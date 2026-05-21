@echo off
setlocal
cd /d "%~dp0..\.."

set "VISION_PERF_LOG=1"
set "VISION_BACKEND=native"
if not defined MOUSE_INJECTION_BACKEND set "MOUSE_INJECTION_BACKEND=sendinput"
if not defined VISION_CAPTURE_FPS set "VISION_CAPTURE_FPS=140"
if not defined VISION_QUIT_KEY set "VISION_QUIT_KEY=Q"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

echo Vision settings: backend=%VISION_BACKEND% capture_fps=%VISION_CAPTURE_FPS% quit_key=%VISION_QUIT_KEY% debug=off mouse_input=%MOUSE_INJECTION_BACKEND%
echo Launching native mouse mode
%PYTHON_CMD% main.py --controller-mode mouse --vision-backend %VISION_BACKEND% --perf-log
pause
