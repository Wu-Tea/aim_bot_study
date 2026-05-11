@echo off
setlocal
cd /d "%~dp0"

set "AUTO_FIRE_ARG="
set "AUTO_FIRE_LABEL=config/default"

echo Select AutoFire output:
echo 1. RB
echo 2. RT
echo Press Enter to use config.toml/default.
set /p "FIRE_CHOICE=Choose [1/2/Enter]: "

if /I "%FIRE_CHOICE%"=="1" (
    set "AUTO_FIRE_ARG=--auto-fire-output RB"
    set "AUTO_FIRE_LABEL=RB"
) else if /I "%FIRE_CHOICE%"=="2" (
    set "AUTO_FIRE_ARG=--auto-fire-output RT"
    set "AUTO_FIRE_LABEL=RT"
) else if not "%FIRE_CHOICE%"=="" (
    echo Invalid selection. Using config.toml/default.
)

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

echo Vision settings: config.toml defaults with existing VISION_* environment overrides.
echo Launching gamepad mode with AutoFire=%AUTO_FIRE_LABEL%
if /I "%ENABLE_RECOIL_RUNTIME%"=="1" (
    if not defined RECOIL_GAME set "RECOIL_GAME=cod22"
    if not defined RECOIL_PROFILE_DIR set "RECOIL_PROFILE_DIR=%cd%\artifacts\recoil_profiles"
    if not defined RECOIL_SIGNATURE_DIR set "RECOIL_SIGNATURE_DIR=%cd%\artifacts\weapon_signatures"
    if not defined RECOIL_STATE_FILE set "RECOIL_STATE_FILE=%cd%\artifacts\recoil_state\%RECOIL_GAME%-latest-state.json"
    if not defined RECOIL_RECOGNIZER_FPS set "RECOIL_RECOGNIZER_FPS=20"
    echo Recoil runtime enabled for %RECOIL_GAME%
    %PYTHON_CMD% tools\recoil_runtime_launcher.py --game %RECOIL_GAME% --profile-dir "%RECOIL_PROFILE_DIR%" --signature-dir "%RECOIL_SIGNATURE_DIR%" --state-file "%RECOIL_STATE_FILE%" --recognizer-fps %RECOIL_RECOGNIZER_FPS% --controller-mode gamepad %AUTO_FIRE_ARG%
) else (
    %PYTHON_CMD% main.py --controller-mode gamepad %AUTO_FIRE_ARG%
)
pause
