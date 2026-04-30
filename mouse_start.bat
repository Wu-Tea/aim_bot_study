@echo off
setlocal
cd /d "%~dp0"

set "VISION_PERF_LOG=1"
set "VISION_BACKEND=native"
if not defined VISION_CAPTURE_FPS set "VISION_CAPTURE_FPS=240"
if not defined VISION_TRACK_FPS set "VISION_TRACK_FPS=160"
if not defined VISION_WARMSCAN_FPS set "VISION_WARMSCAN_FPS=20"
if not defined VISION_SCAN_FPS set "VISION_SCAN_FPS=80"
if not defined VISION_RECOVERY_SCAN_FPS set "VISION_RECOVERY_SCAN_FPS=125"
if not defined VISION_QUIT_KEY set "VISION_QUIT_KEY=0"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

echo Vision settings: backend=%VISION_BACKEND% capture_fps=%VISION_CAPTURE_FPS% track_fps=%VISION_TRACK_FPS% warm_scan_fps=%VISION_WARMSCAN_FPS% scan_fps=%VISION_SCAN_FPS% recovery_scan_fps=%VISION_RECOVERY_SCAN_FPS% quit_key=%VISION_QUIT_KEY% debug=off
echo Launching native mouse mode
%PYTHON_CMD% main.py --controller-mode mouse --vision-backend %VISION_BACKEND% --perf-log
pause
