@echo off
setlocal EnableDelayedExpansion
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

if not defined ENABLE_RECOIL_RUNTIME (
    echo.
    echo Select Recoil runtime:
    echo 1. On
    echo 2. Off
    echo Press Enter to enable recoil runtime.
    if defined GAMEPAD_START_RECOIL_CHOICE_OVERRIDE (
        set "RECOIL_RUNTIME_CHOICE=%GAMEPAD_START_RECOIL_CHOICE_OVERRIDE%"
    ) else (
        set /p "RECOIL_RUNTIME_CHOICE=Choose [1/2/Enter]: "
    )
    set "RECOIL_RUNTIME_CHOICE=!RECOIL_RUNTIME_CHOICE: =!"
    set "RECOIL_RUNTIME_CHOICE=!RECOIL_RUNTIME_CHOICE:~0,1!"
    set "ENABLE_RECOIL_RUNTIME=1"
    if "!RECOIL_RUNTIME_CHOICE!"=="2" set "ENABLE_RECOIL_RUNTIME=0"
    if /I "!RECOIL_RUNTIME_CHOICE!"=="N" set "ENABLE_RECOIL_RUNTIME=0"
    if "!RECOIL_RUNTIME_CHOICE!"=="1" set "ENABLE_RECOIL_RUNTIME=1"
    if not "!RECOIL_RUNTIME_CHOICE!"=="" if not "!RECOIL_RUNTIME_CHOICE!"=="1" if not "!RECOIL_RUNTIME_CHOICE!"=="2" if /I not "!RECOIL_RUNTIME_CHOICE!"=="N" echo Invalid recoil selection. Enabling recoil runtime.
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
    if not defined RECOIL_SIGNATURE_DIR set "RECOIL_SIGNATURE_DIR=%cd%\artifacts\recoil_app\weapons"
    if not defined RECOIL_STATE_FILE set "RECOIL_STATE_FILE=%cd%\artifacts\recoil_state\!RECOIL_GAME!-latest-state.json"
    if not defined RECOIL_RECOGNIZER_FPS set "RECOIL_RECOGNIZER_FPS=20"
    if not defined ENABLE_RECOIL_APP set "ENABLE_RECOIL_APP=1"
    if not defined RECOIL_APP_MODE set "RECOIL_APP_MODE=recoil"
    echo Recoil runtime enabled for !RECOIL_GAME!
    if "%GAMEPAD_START_PRINT_ONLY%"=="1" (
        echo Resolved command: !PYTHON_CMD! tools\recoil_runtime_launcher.py --game !RECOIL_GAME! --profile-dir "!RECOIL_PROFILE_DIR!" --signature-dir "!RECOIL_SIGNATURE_DIR!" --state-file "!RECOIL_STATE_FILE!" --recognizer-fps !RECOIL_RECOGNIZER_FPS! --controller-mode gamepad !AUTO_FIRE_ARG!
        goto end
    )
    !PYTHON_CMD! tools\recoil_runtime_launcher.py --game !RECOIL_GAME! --profile-dir "!RECOIL_PROFILE_DIR!" --signature-dir "!RECOIL_SIGNATURE_DIR!" --state-file "!RECOIL_STATE_FILE!" --recognizer-fps !RECOIL_RECOGNIZER_FPS! --controller-mode gamepad !AUTO_FIRE_ARG!
) else (
    if "%GAMEPAD_START_PRINT_ONLY%"=="1" (
        echo Resolved command: !PYTHON_CMD! main.py --controller-mode gamepad !AUTO_FIRE_ARG!
        goto end
    )
    !PYTHON_CMD! main.py --controller-mode gamepad !AUTO_FIRE_ARG!
)

:end
if not "%GAMEPAD_START_PRINT_ONLY%"=="1" pause
