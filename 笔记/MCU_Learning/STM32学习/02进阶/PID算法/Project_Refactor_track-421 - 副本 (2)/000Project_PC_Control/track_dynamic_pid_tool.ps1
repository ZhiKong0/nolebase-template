param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ArgsForPython
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pythonScript = Join-Path $scriptDir "track_dynamic_pid_tool.py"

if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $pythonScript @ArgsForPython
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    & python $pythonScript @ArgsForPython
} else {
    Write-Error "未找到 py 或 python"
    exit 1
}
