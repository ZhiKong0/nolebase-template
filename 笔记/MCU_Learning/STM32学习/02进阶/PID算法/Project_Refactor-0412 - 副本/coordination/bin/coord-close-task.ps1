$env:COORDINATION_HOST_ROOT = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
& (Join-Path $PSScriptRoot "..\tooling\scripts\coord_close_task.ps1") @args
exit $LASTEXITCODE
