# ============================================================================
# NeuroPace RDNA — Build All C++ Modules
# ============================================================================
# Builds the Telemetry and Actuator modules using CMake + vcpkg.
# Run from the project root: .\scripts\build-all.ps1
# ============================================================================

param(
    [string]$BuildType = "Release",
    [string]$AdlxSdkDir = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  NeuroPace RDNA — Build System" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# ── Telemetry Module ────────────────────────────────────────────────────────
Write-Host "[BUILD] Telemetry Module..." -ForegroundColor Yellow

$telemetryDir = Join-Path $ProjectRoot "telemetry"
$telemetryBuild = Join-Path $telemetryDir "build"

if ($Clean -and (Test-Path $telemetryBuild)) {
    Write-Host "  Cleaning build directory..."
    Remove-Item -Recurse -Force $telemetryBuild
}

if (-not (Test-Path $telemetryBuild)) {
    New-Item -ItemType Directory -Path $telemetryBuild | Out-Null
}

$cmakeArgs = @(
    "-S", $telemetryDir,
    "-B", $telemetryBuild,
    "-DCMAKE_BUILD_TYPE=$BuildType"
)

if ($AdlxSdkDir) {
    $cmakeArgs += "-DADLX_SDK_DIR=$AdlxSdkDir"
    $cmakeArgs += "-DNEUROPACE_USE_ADLX=ON"
}

Write-Host "  Configuring..."
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "  Building..."
cmake --build $telemetryBuild --config $BuildType --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

Write-Host "[BUILD] Telemetry Module — OK" -ForegroundColor Green
Write-Host ""

# ── Actuator Module ─────────────────────────────────────────────────────────
Write-Host "[BUILD] Actuator Module..." -ForegroundColor Yellow

$actuatorDir = Join-Path $ProjectRoot "actuator"
$actuatorBuild = Join-Path $actuatorDir "build"

if ($Clean -and (Test-Path $actuatorBuild)) {
    Write-Host "  Cleaning build directory..."
    Remove-Item -Recurse -Force $actuatorBuild
}

if (-not (Test-Path $actuatorBuild)) {
    New-Item -ItemType Directory -Path $actuatorBuild | Out-Null
}

$cmakeActArgs = @(
    "-S", $actuatorDir,
    "-B", $actuatorBuild,
    "-DCMAKE_BUILD_TYPE=$BuildType"
)

if ($AdlxSdkDir) {
    $cmakeActArgs += "-DADLX_SDK_DIR=$AdlxSdkDir"
    $cmakeActArgs += "-DNEUROPACE_USE_ADLX=ON"
}

Write-Host "  Configuring..."
cmake @cmakeActArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for actuator" }

Write-Host "  Building..."
cmake --build $actuatorBuild --config $BuildType --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed for actuator" }

Write-Host "[BUILD] Actuator Module — OK" -ForegroundColor Green
Write-Host ""

# ── Summary ─────────────────────────────────────────────────────────────────
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  Build Complete!" -ForegroundColor Green
Write-Host "  Telemetry: $telemetryBuild\$BuildType\" -ForegroundColor White
Write-Host "  Actuator:  $actuatorBuild\$BuildType\" -ForegroundColor White
Write-Host "================================================" -ForegroundColor Cyan

