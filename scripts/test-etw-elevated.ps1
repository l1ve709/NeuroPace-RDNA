param(
    [Parameter(Mandatory)][string]$TestExecutable,
    [Parameter(Mandatory)][string]$LogPath
)
$ErrorActionPreference = 'Stop'
$code = 1
Start-Transcript -LiteralPath $LogPath | Out-Null
try {
    & $TestExecutable --capture
    $code = $LASTEXITCODE
} finally {
    Stop-Transcript | Out-Null
}
exit $code
