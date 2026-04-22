[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ArgsList
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pythonExe = "C:\Users\DZ\AppData\Local\Programs\Python\Python313\python.exe"
if (-not (Test-Path $pythonExe)) {
    $pythonExe = "python"
}

$scriptPath = Join-Path $scriptDir "track_adaptive_tuner.py"
if (-not (Test-Path $scriptPath)) {
    throw "track_adaptive_tuner.py not found: $scriptPath"
}

Write-Host "[track-adaptive] python = $pythonExe"
Write-Host "[track-adaptive] script = $scriptPath"

& $pythonExe $scriptPath @ArgsList
exit $LASTEXITCODE
