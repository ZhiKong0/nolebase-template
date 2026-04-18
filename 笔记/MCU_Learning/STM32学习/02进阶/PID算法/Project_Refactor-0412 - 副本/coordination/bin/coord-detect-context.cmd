@echo off
setlocal
set COORDINATION_HOST_ROOT=%~dp0..\..
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tooling\scripts\coord_detect_context.ps1" %*
