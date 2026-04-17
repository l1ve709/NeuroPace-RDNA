# ============================================================================
# NeuroPace RDNA — Run All Modules
# ============================================================================
# Orchestrates all 4 modules of the NeuroPace RDNA system.
#
# Usage:
#   .\scripts\run-all.ps1              # Production (requires Admin for ETW)
#   .\scripts\run-all.ps1 -Mock        # Development (mock telemetry, no Admin)
#
# Modules started:
#   1. Telemetry (C++) or Mock Telemetry (Python)
#   2. AI Prediction Engine (Python)
#   3. Actuator (C++) — optional, only when game is targeted
#   4. Dashboard (Node.js + Vue.js)
#
# Press Ctrl+C to stop all modules gracefully.
# ============================================================================

param(
    [switch]$Mock,
    [switch]$NoDashboard,
    [switch]$NoActuator,
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

Write-Host ""
Write-Host "+----------------------------------------------------------+" -ForegroundColor Cyan
Write-Host "|                                                          |" -ForegroundColor Cyan
Write-Host "|   NeuroPace RDNA -- System Launcher v0.1.0              |" -ForegroundColor Cyan
Write-Host "|   AMD RDNA3 Frame-Drop Prevention Engine                |" -ForegroundColor Cyan
Write-Host "|                                                          |" -ForegroundColor Cyan
Write-Host "+----------------------------------------------------------+" -ForegroundColor Cyan
Write-Host ""

if ($Mock) {
    Write-Host "  Mode: DEVELOPMENT (Mock Telemetry)" -ForegroundColor Yellow
} else {
    Write-Host "  Mode: PRODUCTION (ETW + ADLX)" -ForegroundColor Green
    # Check admin privileges for production mode
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator
    )
    if (-not $isAdmin) {
        Write-Host ""
        Write-Host "  ERROR: Production mode requires Administrator privileges!" -ForegroundColor Red
        Write-Host "  ETW kernel tracing needs elevated access." -ForegroundColor Red
        Write-Host ""
        Write-Host "  Use -Mock flag for development mode:" -ForegroundColor Yellow
        Write-Host "    .\scripts\run-all.ps1 -Mock" -ForegroundColor White
        Write-Host ""
        exit 1
    }
}

Write-Host ""

# ── Process Tracking ────────────────────────────────────────────────────────

$processes = @()

function Start-Module {
    param(
        [string]$Name,
        [string]$Command,
        [string]$WorkDir,
        [string]$Color = "White"
    )

    Write-Host "  Starting: $Name..." -ForegroundColor $Color

    $proc = Start-Process -FilePath "cmd.exe" `
        -ArgumentList "/c", "title NeuroPace - $Name && cd /d `"$WorkDir`" && $Command" `
        -PassThru `
        -WindowStyle Normal

    if ($proc -and !$proc.HasExited) {
        $script:processes += $proc
        Write-Host "    PID: $($proc.Id)" -ForegroundColor DarkGray
        return $proc
    } else {
        Write-Host "    FAILED to start $Name" -ForegroundColor Red
        return $null
    }
}

# ── Module 1: Telemetry ────────────────────────────────────────────────────

if ($Mock) {
    $telemetryProc = Start-Module `
        -Name "Mock Telemetry" `
        -Command "python `"$ProjectRoot\scripts\mock-telemetry.py`"" `
        -WorkDir $ProjectRoot `
        -Color "Yellow"
} else {
    $telemetryExe = Join-Path $ProjectRoot "telemetry\build\$BuildType\neuropace-telemetry.exe"
    if (-not (Test-Path $telemetryExe)) {
        Write-Host "  Telemetry executable not found: $telemetryExe" -ForegroundColor Red
        Write-Host "  Run .\scripts\build-all.ps1 first." -ForegroundColor Yellow
        exit 1
    }
    $telemetryProc = Start-Module `
        -Name "Telemetry (ETW+ADLX)" `
        -Command "`"$telemetryExe`"" `
        -WorkDir (Join-Path $ProjectRoot "telemetry") `
        -Color "Green"
}

# Wait for pipe to be created
Write-Host "  Waiting for telemetry pipe..." -ForegroundColor DarkGray
Start-Sleep -Seconds 2

# ── Module 2: AI Engine ────────────────────────────────────────────────────

$aiEngineProc = Start-Module `
    -Name "AI Prediction Engine" `
    -Command "python `"$ProjectRoot\ai-engine\src\main.py`"" `
    -WorkDir (Join-Path $ProjectRoot "ai-engine") `
    -Color "Magenta"

Start-Sleep -Seconds 1

# ── Module 3: Actuator ─────────────────────────────────────────────────────

if (-not $NoActuator) {
    $actuatorExe = Join-Path $ProjectRoot "actuator\build\$BuildType\neuropace-actuator.exe"
    if (Test-Path $actuatorExe) {
        $actuatorProc = Start-Module `
            -Name "Actuator (Scheduler)" `
            -Command "`"$actuatorExe`"" `
            -WorkDir (Join-Path $ProjectRoot "actuator") `
            -Color "Blue"
    } else {
        Write-Host "  Actuator not built -- skipping (run build-all.ps1)" -ForegroundColor DarkGray
    }
}

# ── Module 4: Dashboard ────────────────────────────────────────────────────

if (-not $NoDashboard) {
    $dashboardProc = Start-Module `
        -Name "Dashboard (Web UI)" `
        -Command "node `"$ProjectRoot\dashboard\server\index.js`"" `
        -WorkDir (Join-Path $ProjectRoot "dashboard") `
        -Color "Cyan"

    Start-Sleep -Seconds 1
    Write-Host ""
    Write-Host "  Dashboard: http://localhost:3200" -ForegroundColor Cyan
}

# ── Running Status ──────────────────────────────────────────────────────────

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  All modules started! ($($processes.Count) processes)" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Press Ctrl+C to stop all modules" -ForegroundColor Yellow
Write-Host ""

# ── Wait for shutdown ───────────────────────────────────────────────────────

try {
    while ($true) {
        # Check if any process has exited unexpectedly
        $alive = $processes | Where-Object { !$_.HasExited }
        if ($alive.Count -eq 0) {
            Write-Host "  All processes have exited." -ForegroundColor Yellow
            break
        }
        Start-Sleep -Seconds 2
    }
} finally {
    # ── Graceful Shutdown ───────────────────────────────────────────────
    Write-Host ""
    Write-Host "  Stopping all modules..." -ForegroundColor Yellow

    foreach ($proc in $processes) {
        if (!$proc.HasExited) {
            try {
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                Write-Host "    Stopped PID $($proc.Id)" -ForegroundColor DarkGray
            } catch {
                # Process already exited
            }
        }
    }

    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "  NeuroPace RDNA -- All modules stopped" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
}
