@echo off
setlocal
cd /d "%~dp0"

set "VISION_PERF_LOG=1"
set "VISION_BACKEND=native"
if not defined VISION_CAPTURE_FPS set "VISION_CAPTURE_FPS=140"
if not defined VISION_QUIT_KEY set "VISION_QUIT_KEY=0"
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

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

echo Vision settings: backend=%VISION_BACKEND% capture_fps=%VISION_CAPTURE_FPS% quit_key=%VISION_QUIT_KEY%
echo Launching gamepad mode with AutoFire=%AUTO_FIRE_OUTPUT%
if /I "%ENABLE_RECOIL_RUNTIME%"=="1" (
    if not defined RECOIL_GAME set "RECOIL_GAME=cod22"
    if not defined RECOIL_PROFILE_DIR set "RECOIL_PROFILE_DIR=%cd%\artifacts\recoil_profiles"
    if not defined RECOIL_SIGNATURE_DIR set "RECOIL_SIGNATURE_DIR=%cd%\artifacts\weapon_signatures"
    if not defined RECOIL_STATE_FILE set "RECOIL_STATE_FILE=%cd%\artifacts\recoil_state\%RECOIL_GAME%-latest-state.json"
    if not defined RECOIL_RECOGNIZER_FPS set "RECOIL_RECOGNIZER_FPS=20"
    echo Recoil runtime enabled for %RECOIL_GAME%
    %PYTHON_CMD% tools\recoil_runtime_launcher.py --game %RECOIL_GAME% --profile-dir "%RECOIL_PROFILE_DIR%" --signature-dir "%RECOIL_SIGNATURE_DIR%" --state-file "%RECOIL_STATE_FILE%" --recognizer-fps %RECOIL_RECOGNIZER_FPS% --controller-mode gamepad --auto-fire-output %AUTO_FIRE_OUTPUT% --vision-backend %VISION_BACKEND%
) else (
    %PYTHON_CMD% main.py --controller-mode gamepad --auto-fire-output %AUTO_FIRE_OUTPUT% --vision-backend %VISION_BACKEND% --perf-log
)
pause
