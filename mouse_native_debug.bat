@echo off
setlocal
cd /d "%~dp0"

set "VISION_PERF_LOG=1"
set "VISION_BACKEND=native"
if not defined VISION_CAPTURE_FPS set "VISION_CAPTURE_FPS=140"
if not defined VISION_QUIT_KEY set "VISION_QUIT_KEY=0"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

echo Vision settings: backend=%VISION_BACKEND% capture_fps=%VISION_CAPTURE_FPS% quit_key=%VISION_QUIT_KEY% debug=on debug_save=on
echo Launching native mouse debug mode
%PYTHON_CMD% main.py --controller-mode mouse --vision-backend %VISION_BACKEND% --vision-debug --vision-debug-save --perf-log
pause
