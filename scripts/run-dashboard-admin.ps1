#Requires -RunAsAdministrator
# ============================================================================
# NeuroPace RDNA — Launch the Dashboard server elevated
# ============================================================================

$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $ProjectRoot "dashboard")
node server/index.js
