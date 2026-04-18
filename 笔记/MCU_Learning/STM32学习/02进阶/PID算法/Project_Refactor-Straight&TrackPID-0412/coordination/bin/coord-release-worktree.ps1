$env:COORDINATION_HOST_ROOT = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
& (Join-Path $PSScriptRoot "..\tooling\scripts\coord_release_worktree.ps1") @args
exit $LASTEXITCODE
