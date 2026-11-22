# Run the Windows pipeline regression from any working directory.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pythonCommand = if ($env:NEUROPACE_PYTHON) { $env:NEUROPACE_PYTHON } else { 'python' }

Push-Location -LiteralPath $projectRoot
try {
    if (-not (Test-Path -LiteralPath 'ai-engine/models/frame_drop_rf.onnx')) {
        & $pythonCommand ai-engine/training/train_rf.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    Push-Location -LiteralPath dashboard
    try {
        npm run test:integration
        exit $LASTEXITCODE
    } finally {
        Pop-Location
    }
} finally {
    Pop-Location
}
