@echo off
setlocal
cd /d "%~dp0"

set "VISION_PERF_LOG=1"
if not defined VISION_FAST_PATH set "VISION_FAST_PATH=1"
if not defined VISION_FAST_PREPROCESSOR set "VISION_FAST_PREPROCESSOR=cpu"
if not defined VISION_CAPTURE_FPS set "VISION_CAPTURE_FPS=80"
if not defined VISION_IDLE_CAPTURE_FPS set "VISION_IDLE_CAPTURE_FPS=10"
set "AUTO_FIRE_OUTPUT=RB"

echo Select AutoFire output:
echo 1. RB
echo 2. RT
set /p "FIRE_CHOICE=Choose [1/2] (default 1): "

if /I "%FIRE_CHOICE%"=="2" (
    set "AUTO_FIRE_OUTPUT=RT"
) else if not "%FIRE_CHOICE%"=="" if /I not "%FIRE_CHOICE%"=="1" (
    echo Invalid selection. Using default: RB
)

echo Select Vision preprocessor:
echo 1. CPU (stable)
echo 2. Native (experimental)
set /p "VISION_CHOICE=Choose [1/2] (current %VISION_FAST_PREPROCESSOR%): "

if /I "%VISION_CHOICE%"=="2" (
    set "VISION_FAST_PREPROCESSOR=native"
) else if /I "%VISION_CHOICE%"=="1" (
    set "VISION_FAST_PREPROCESSOR=cpu"
) else if not "%VISION_CHOICE%"=="" (
    echo Invalid selection. Keeping current Vision preprocessor: %VISION_FAST_PREPROCESSOR%
)

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

echo Vision settings: fast_path=%VISION_FAST_PATH% preprocessor=%VISION_FAST_PREPROCESSOR% capture_fps=%VISION_CAPTURE_FPS% idle_capture_fps=%VISION_IDLE_CAPTURE_FPS%
if /I "%VISION_FAST_PREPROCESSOR%"=="native" (
    echo Note: native mode still falls back to CPU preprocessing if vision_native is unavailable.
)
echo Launching gamepad mode with AutoFire=%AUTO_FIRE_OUTPUT%
%PYTHON_CMD% main.py --controller-mode gamepad --auto-fire-output %AUTO_FIRE_OUTPUT%
pause
