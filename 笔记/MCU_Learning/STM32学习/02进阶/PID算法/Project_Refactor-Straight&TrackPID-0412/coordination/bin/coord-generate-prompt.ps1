$env:COORDINATION_HOST_ROOT = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
& (Join-Path $PSScriptRoot "..\tooling\scripts\coord_generate_prompt.ps1") @args
exit $LASTEXITCODE
