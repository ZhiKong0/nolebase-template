param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ScriptArgs
)

$scriptPath = Join-Path $PSScriptRoot 'experiment_logger.py'

if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $scriptPath @ScriptArgs
    exit $LASTEXITCODE
}

if (Get-Command python -ErrorAction SilentlyContinue) {
    & python $scriptPath @ScriptArgs
    exit $LASTEXITCODE
}

throw "Neither 'py' nor 'python' is available"
