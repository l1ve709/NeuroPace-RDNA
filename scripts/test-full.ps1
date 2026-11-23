[CmdletBinding()]
param([string]$AdlxSdkDir, [string]$CmakePrefixPath, [string]$LogDir)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $root 'build/full-regression'
$python = if ($env:NEUROPACE_PYTHON) { $env:NEUROPACE_PYTHON } else { 'python' }
if (-not $LogDir) { $LogDir = Join-Path $buildRoot 'logs' }
$LogDir = (New-Item -ItemType Directory -Path $LogDir -Force).FullName
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$etwLog = Join-Path $LogDir "etw-$runId.log"
$failures = [System.Collections.Generic.List[string]]::new()

function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command exited with code $LASTEXITCODE" }
}

function Invoke-Stage {
    param([string]$Name, [scriptblock]$Body)
    Write-Host "--- $Name ---"
    try {
        & $Body
        Write-Host "PASS $Name"
    } catch {
        $failures.Add($Name)
        Write-Host "FAIL $Name - $_" -ForegroundColor Red
    }
}

function Build-Module {
    param([string]$Module, [string]$Build, [string[]]$Extra)
    $options = @('-S', (Join-Path $root $Module), '-B', $Build,
        '-DNEUROPACE_BUILD_TESTS=ON', '-DCMAKE_BUILD_TYPE=Release')
    if ($CmakePrefixPath) { $options += "-DCMAKE_PREFIX_PATH=$CmakePrefixPath" }
    if ($env:VCPKG_ROOT) {
        $options += "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $env:VCPKG_ROOT 'scripts/buildsystems/vcpkg.cmake')"
    }
    Invoke-Checked cmake ($options + $Extra)
    Invoke-Checked cmake @('--build', $Build, '--config', 'Release', '--parallel', '2')
}

Start-Transcript -LiteralPath (Join-Path $LogDir "regression-$runId.log") | Out-Null
Push-Location -LiteralPath $root
try {
    $telemetry = Join-Path $buildRoot 'telemetry'
    Invoke-Stage 'Telemetry build and unit tests' {
        Build-Module telemetry $telemetry @('-DNEUROPACE_USE_ADLX=OFF')
        Invoke-Checked ctest @('--test-dir', $telemetry, '-C', 'Release', '-E',
            '^etw_session_start$', '--output-on-failure', '--no-tests=error')
    }
    if (-not $failures.Contains('Telemetry build and unit tests')) {
        Invoke-Stage 'Elevated ETW event capture' {
            $testExe = Join-Path $telemetry 'Release/test_etw.exe'
            if (-not (Test-Path -LiteralPath $testExe)) { $testExe = Join-Path $telemetry 'test_etw.exe' }
            $worker = Join-Path $PSScriptRoot 'test-etw-elevated.ps1'
            $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -TestExecutable "{1}" -LogPath "{2}"' -f $worker, $testExe, $etwLog
            $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
            $principal = [Security.Principal.WindowsPrincipal]::new($identity)
            $launch = @{ ArgumentList = $arguments; WindowStyle = 'Hidden'; PassThru = $true }
            if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
                Write-Host 'Approve Windows UAC to run the ETW capture.'
                $launch.Verb = 'RunAs'
            }
            $process = Start-Process powershell.exe @launch
            if (-not $process.WaitForExit(25000)) { throw "ETW worker timed out. See $etwLog" }
            if (Test-Path -LiteralPath $etwLog) { Get-Content -LiteralPath $etwLog }
            if ($process.ExitCode -ne 0) { throw "ETW capture exited with code $($process.ExitCode). See $etwLog" }
        }
    }
    Invoke-Stage 'Actuator build and unit tests' {
        $build = Join-Path $buildRoot 'actuator'
        Build-Module actuator $build @('-DNEUROPACE_USE_ADLX=OFF')
        Invoke-Checked ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--no-tests=error')
    }
    if ($AdlxSdkDir) {
        Invoke-Stage 'ADLX telemetry build' {
            Build-Module telemetry (Join-Path $buildRoot 'telemetry-adlx') @(
                '-DNEUROPACE_USE_ADLX=ON', "-DADLX_SDK_DIR=$AdlxSdkDir")
        }
    }
    Invoke-Stage 'Python unit tests' {
        Invoke-Checked $python @('-m', 'pytest', '-q', 'ai-engine/tests')
    }
    Invoke-Stage 'Train and validate the ONNX model' {
        Invoke-Checked $python @('ai-engine/training/train_rf.py')
    }
    Invoke-Stage 'Dashboard and Windows pipeline' {
        Push-Location -LiteralPath dashboard
        try {
            Invoke-Checked npm @('ci', '--ignore-scripts')
            Invoke-Checked npm @('run', 'check')
            Invoke-Checked npm @('run', 'test:integration')
            Invoke-Checked npm @('audit', '--omit=dev')
        } finally { Pop-Location }
    }
    if ($failures.Count) {
        Write-Host "Failed stages: $($failures -join ', ')" -ForegroundColor Red
        exit 1
    }
    Write-Host 'All requested regression stages passed.' -ForegroundColor Green
} finally {
    Pop-Location
    Stop-Transcript | Out-Null
}
