# ============================================================================
# NeuroPace RDNA — Install Dependencies
# ============================================================================

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  NeuroPace RDNA — Dependency Installer" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# ── Check vcpkg ─────────────────────────────────────────────────────────
if (-not $env:VCPKG_ROOT) {
    Write-Host "[CHECK] VCPKG_ROOT not set." -ForegroundColor Yellow
    Write-Host "  Install vcpkg: https://vcpkg.io/en/getting-started" -ForegroundColor White
    Write-Host "  Then set VCPKG_ROOT environment variable." -ForegroundColor White
    Write-Host ""
}

# ── Install C++ Dependencies via vcpkg ──────────────────────────────────
if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) {
    Write-Host "[VCPKG] Installing C++ dependencies..." -ForegroundColor Yellow

    $vcpkg = Join-Path $env:VCPKG_ROOT "vcpkg.exe"

    & $vcpkg install nlohmann-json:x64-windows
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] vcpkg install failed" -ForegroundColor Red }

    Write-Host "[VCPKG] Done." -ForegroundColor Green
} else {
    Write-Host "[SKIP] vcpkg not found — skipping C++ deps" -ForegroundColor Yellow
}

Write-Host ""

# ── Python Dependencies ────────────────────────────────────────────────
$aiEngineDir = Join-Path $ProjectRoot "ai-engine"
$requirements = Join-Path $aiEngineDir "requirements.txt"

if (Test-Path $requirements) {
    Write-Host "[PYTHON] Installing AI Engine dependencies..." -ForegroundColor Yellow
    python -m pip install -r $requirements
    Write-Host "[PYTHON] Done." -ForegroundColor Green
} else {
    Write-Host "[SKIP] ai-engine/requirements.txt not found yet" -ForegroundColor Yellow
}

Write-Host ""

# ── Node.js Dependencies ───────────────────────────────────────────────
$serverDir = Join-Path $ProjectRoot "dashboard\server"
$clientDir = Join-Path $ProjectRoot "dashboard\client"

if (Test-Path (Join-Path $serverDir "package.json")) {
    Write-Host "[NODE] Installing Dashboard Server dependencies..." -ForegroundColor Yellow
    Push-Location $serverDir
    npm install
    Pop-Location
    Write-Host "[NODE] Server deps — Done." -ForegroundColor Green
}

if (Test-Path (Join-Path $clientDir "package.json")) {
    Write-Host "[NODE] Installing Dashboard Client dependencies..." -ForegroundColor Yellow
    Push-Location $clientDir
    npm install
    Pop-Location
    Write-Host "[NODE] Client deps — Done." -ForegroundColor Green
}

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  All dependencies installed!" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Cyan
