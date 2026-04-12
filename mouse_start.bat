@echo off
setlocal
cd /d "%~dp0"

set "VISION_PERF_LOG=1"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

echo Launching mouse mode
%PYTHON_CMD% main.py --controller-mode mouse
pause
