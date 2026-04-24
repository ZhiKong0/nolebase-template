$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PythonExe = "C:\Users\DZ\AppData\Local\Programs\Python\Python313\python.exe"
$ScriptPath = Join-Path $ScriptDir "experiment_score_watch.py"

if (-not (Test-Path $PythonExe)) {
    $PythonExe = "python"
}

Write-Host "[score-watch] python = $PythonExe"
Write-Host "[score-watch] script = $ScriptPath"

& $PythonExe -u $ScriptPath @args
