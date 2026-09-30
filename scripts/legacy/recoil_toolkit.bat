@echo off
setlocal
cd /d "%~dp0..\.."
set "PYTHONPATH=%CD%\python;%PYTHONPATH%"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

%PYTHON_CMD% -m tools.recoil_toolkit_console %*
exit /b %errorlevel%
