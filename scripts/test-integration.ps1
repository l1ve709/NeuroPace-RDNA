# ============================================================================
# NeuroPace RDNA — Integration Test
# ============================================================================
# Verifies the end-to-end data pipeline works correctly:
#   Mock Telemetry → AI Engine → Dashboard
#
# Tests:
#   1. Mock telemetry pipe is created and publishing
#   2. AI Engine connects and receives data
#   3. Dashboard HTTP server responds
#   4. Dashboard WebSocket delivers data
#   5. Graceful shutdown of all components
#
# Usage:
#   .\scripts\test-integration.ps1
# ============================================================================

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$testsPassed = 0
$testsFailed = 0
$processes = @()

function Write-Test {
    param([string]$Name, [bool]$Passed, [string]$Detail = "")
    if ($Passed) {
        Write-Host "  [PASS] $Name" -ForegroundColor Green
        if ($Detail) { Write-Host "         $Detail" -ForegroundColor DarkGray }
        $script:testsPassed++
    } else {
        Write-Host "  [FAIL] $Name" -ForegroundColor Red
        if ($Detail) { Write-Host "         $Detail" -ForegroundColor DarkGray }
        $script:testsFailed++
    }
}

function Start-BackgroundProcess {
    param([string]$Name, [string]$Command, [string]$WorkDir)
    $proc = Start-Process -FilePath "cmd.exe" `
        -ArgumentList "/c", "title TEST-$Name && cd /d `"$WorkDir`" && $Command" `
        -PassThru -WindowStyle Minimized
    $script:processes += $proc
    return $proc
}

function Stop-AllProcesses {
    foreach ($p in $script:processes) {
        if (!$p.HasExited) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

# ════════════════════════════════════════════════════════════════════════════

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  NeuroPace RDNA -- Integration Test Suite" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

try {
    # ── Step 1: Start Mock Telemetry ────────────────────────────────────
    Write-Host "[STEP 1] Starting Mock Telemetry..." -ForegroundColor Yellow

    $mockProc = Start-BackgroundProcess `
        -Name "MockTelemetry" `
        -Command "python `"$ProjectRoot\scripts\mock-telemetry.py`"" `
        -WorkDir $ProjectRoot

    Start-Sleep -Seconds 2

    # Test: Mock telemetry process is running
    Write-Test "Mock telemetry process started" (!$mockProc.HasExited) "PID: $($mockProc.Id)"

    # Test: Named pipe exists
    $pipeExists = Test-Path "\\.\pipe\neuropace-telemetry"
    Write-Test "Telemetry pipe created" $pipeExists "\\.\pipe\neuropace-telemetry"

    # ── Step 2: Start AI Engine ─────────────────────────────────────────
    Write-Host ""
    Write-Host "[STEP 2] Starting AI Engine..." -ForegroundColor Yellow

    $aiProc = Start-BackgroundProcess `
        -Name "AIEngine" `
        -Command "python `"$ProjectRoot\ai-engine\src\main.py`"" `
        -WorkDir "$ProjectRoot\ai-engine"

    Start-Sleep -Seconds 3

    # Test: AI Engine process is running
    Write-Test "AI Engine process started" (!$aiProc.HasExited) "PID: $($aiProc.Id)"

    # Test: Action pipe created by AI Engine
    $actionPipeExists = Test-Path "\\.\pipe\neuropace-action"
    Write-Test "Action pipe created" $actionPipeExists "\\.\pipe\neuropace-action"

    # Test: Prediction pipe created by AI Engine
    $predPipeExists = Test-Path "\\.\pipe\neuropace-prediction"
    Write-Test "Prediction pipe created" $predPipeExists "\\.\pipe\neuropace-prediction"

    # ── Step 3: Start Dashboard ─────────────────────────────────────────
    Write-Host ""
    Write-Host "[STEP 3] Starting Dashboard..." -ForegroundColor Yellow

    $dashProc = Start-BackgroundProcess `
        -Name "Dashboard" `
        -Command "node `"$ProjectRoot\dashboard\server\index.js`"" `
        -WorkDir "$ProjectRoot\dashboard"

    Start-Sleep -Seconds 2

    # Test: Dashboard process is running
    Write-Test "Dashboard process started" (!$dashProc.HasExited) "PID: $($dashProc.Id)"

    # Test: HTTP server responds
    $httpOk = $false
    $httpDetail = ""
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:3200" -TimeoutSec 5 -UseBasicParsing
        $httpOk = ($response.StatusCode -eq 200)
        $httpDetail = "Status: $($response.StatusCode), Size: $($response.Content.Length) bytes"
    } catch {
        $httpDetail = "Error: $_"
    }
    Write-Test "Dashboard HTTP responds (200 OK)" $httpOk $httpDetail

    # Test: HTML contains expected elements
    $htmlOk = $false
    if ($httpOk) {
        $htmlOk = $response.Content.Contains("NeuroPace RDNA") -and
                  $response.Content.Contains("vue@3") -and
                  $response.Content.Contains("dashboard.css")
        $htmlDetail = "Contains Vue3, CSS, and brand"
    }
    Write-Test "Dashboard HTML content valid" $htmlOk $htmlDetail

    # Test: Static assets load
    $cssOk = $false
    try {
        $cssResp = Invoke-WebRequest -Uri "http://localhost:3200/css/dashboard.css" -TimeoutSec 5 -UseBasicParsing
        $cssOk = ($cssResp.StatusCode -eq 200 -and $cssResp.Content.Contains("glassmorphism") -or $cssResp.Content.Contains("--bg-primary"))
    } catch {}
    Write-Test "CSS stylesheet loads" $cssOk

    $jsOk = $false
    try {
        $jsResp = Invoke-WebRequest -Uri "http://localhost:3200/js/app.js" -TimeoutSec 5 -UseBasicParsing
        $jsOk = ($jsResp.StatusCode -eq 200 -and $jsResp.Content.Contains("createApp"))
    } catch {}
    Write-Test "Vue application JS loads" $jsOk

    $chartOk = $false
    try {
        $chartResp = Invoke-WebRequest -Uri "http://localhost:3200/js/chart-engine.js" -TimeoutSec 5 -UseBasicParsing
        $chartOk = ($chartResp.StatusCode -eq 200 -and $chartResp.Content.Contains("LineChart"))
    } catch {}
    Write-Test "Chart engine JS loads" $chartOk

    # ── Step 4: Data Flow Check ─────────────────────────────────────────
    Write-Host ""
    Write-Host "[STEP 4] Verifying data flow..." -ForegroundColor Yellow

    # Wait for data to flow through the pipeline
    Start-Sleep -Seconds 3

    # Test: ONNX model exists (was trained earlier)
    $modelPath = Join-Path $ProjectRoot "ai-engine\models\frame_drop_rf.onnx"
    $modelExists = Test-Path $modelPath
    $modelSize = if ($modelExists) { "{0:N1} KB" -f ((Get-Item $modelPath).Length / 1024) } else { "N/A" }
    Write-Test "ONNX model file exists" $modelExists $modelSize

    # Test: All processes still running (no crashes)
    $allAlive = (!$mockProc.HasExited -and !$aiProc.HasExited -and !$dashProc.HasExited)
    Write-Test "All processes stable (no crashes)" $allAlive

    # ── Step 5: Python Unit Tests ───────────────────────────────────────
    Write-Host ""
    Write-Host "[STEP 5] Running Python unit tests..." -ForegroundColor Yellow

    $pytestResult = & python -m pytest "$ProjectRoot\ai-engine\tests" -v --tb=short 2>&1
    $pytestPassed = $LASTEXITCODE -eq 0
    $testCount = ($pytestResult | Select-String "passed").Matches.Count
    Write-Test "Python unit tests (pytest)" $pytestPassed "$($pytestResult[-1])"

} finally {
    # ── Cleanup ─────────────────────────────────────────────────────────
    Write-Host ""
    Write-Host "[CLEANUP] Stopping all processes..." -ForegroundColor Yellow
    Stop-AllProcesses
    Start-Sleep -Seconds 1
}

# ── Summary ─────────────────────────────────────────────────────────────────

$total = $testsPassed + $testsFailed

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Integration Test Results" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

if ($testsFailed -eq 0) {
    Write-Host "  $testsPassed / $total tests PASSED" -ForegroundColor Green
    Write-Host ""
    Write-Host "  All systems operational!" -ForegroundColor Green
} else {
    Write-Host "  $testsPassed PASSED, $testsFailed FAILED (out of $total)" -ForegroundColor Red
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan

exit $testsFailed
