@echo off
setlocal
set "APP_HOME=%LocalAppData%\MCUBuildFlashGUI"
if not exist "%APP_HOME%" mkdir "%APP_HOME%"
set "PYTHONPYCACHEPREFIX=%APP_HOME%\pycache"
cd /d "%APP_HOME%"

if exist "%LocalAppData%\Programs\Python\Python313\pythonw.exe" (
  start "" "%LocalAppData%\Programs\Python\Python313\pythonw.exe" "%~dp0mcu_build_flash_gui.py"
  exit /b
)

if exist "%LocalAppData%\Programs\Python\Python313\python.exe" (
  start "" "%LocalAppData%\Programs\Python\Python313\python.exe" "%~dp0mcu_build_flash_gui.py"
) else (
  start "" pythonw "%~dp0mcu_build_flash_gui.py"
)
