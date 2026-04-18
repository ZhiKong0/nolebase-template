@echo off
setlocal
set COORDINATION_HOST_ROOT=%~dp0..\..
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tooling\scripts\coord_init_project.ps1" %*
