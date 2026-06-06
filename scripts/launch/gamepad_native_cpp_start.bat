@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0..\.."

set "EXE=native\vision_native\build\Release\cod_native_runtime.exe"
if not exist "%EXE%" (
  echo Native C++ runtime not found. Run tools\build_native_vision.ps1 first.
  exit /b 1
)

set "AUTO_FIRE_ARG="
set "AUTO_FIRE_LABEL=config/default"

echo Select AutoFire output:
echo 1. RB
echo 2. RT
echo Press Enter to use config.toml/default.
if defined GAMEPAD_START_FIRE_CHOICE_OVERRIDE (
  set "FIRE_CHOICE=%GAMEPAD_START_FIRE_CHOICE_OVERRIDE%"
) else (
  set /p "FIRE_CHOICE=Choose [1/2/Enter]: "
)
set "FIRE_CHOICE=!FIRE_CHOICE: =!"
set "FIRE_CHOICE=!FIRE_CHOICE:~0,1!"

if /I "!FIRE_CHOICE!"=="1" (
  set "AUTO_FIRE_ARG=--auto-fire-output RB"
  set "AUTO_FIRE_LABEL=RB"
) else if /I "!FIRE_CHOICE!"=="2" (
  set "AUTO_FIRE_ARG=--auto-fire-output RT"
  set "AUTO_FIRE_LABEL=RT"
) else if not "!FIRE_CHOICE!"=="" (
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

echo Native vision settings: config.toml defaults.
echo Launching native C++ gamepad runtime with AutoFire=!AUTO_FIRE_LABEL!

if /I "%ENABLE_RECOIL_RUNTIME%"=="1" (
  if not defined RECOIL_GAME set "RECOIL_GAME=cod22"
  if not defined RECOIL_PROFILE_DIR set "RECOIL_PROFILE_DIR=%cd%\artifacts\recoil_profiles"
  if not defined RECOIL_CALIBRATION_DIR set "RECOIL_CALIBRATION_DIR=%cd%\artifacts\recoil_calibration"
  if not defined RECOIL_WEAPON_DIR set "RECOIL_WEAPON_DIR=%cd%\artifacts\recoil_app\weapons"
  if not defined RECOIL_STATE_FILE set "RECOIL_STATE_FILE=%cd%\artifacts\recoil_app\current_weapon.json"
  if not defined RECOIL_RECOGNIZER_STATE_PATH set "RECOIL_RECOGNIZER_STATE_PATH=!RECOIL_STATE_FILE!"
  if not defined RECOIL_NATIVE_RECOGNIZER set "RECOIL_NATIVE_RECOGNIZER=1"
  if not defined RECOIL_CLEAR_STATE_ON_START set "RECOIL_CLEAR_STATE_ON_START=1"
  echo Recoil profile selection enabled for !RECOIL_GAME!
  echo Recoil current weapon state: !RECOIL_RECOGNIZER_STATE_PATH!
  echo Recoil weapon OCR: press Y after launch to recognize the switched weapon.
) else (
  set "RECOIL_RECOGNIZER_STATE_PATH="
  echo Recoil profile selection disabled.
)

if "%GAMEPAD_START_PRINT_ONLY%"=="1" (
  echo Resolved command: "%EXE%" --config config.toml --perf-log !AUTO_FIRE_ARG!
  goto end
)

if /I "%ENABLE_RECOIL_RUNTIME%"=="1" if /I "!RECOIL_CLEAR_STATE_ON_START!"=="1" if exist "!RECOIL_RECOGNIZER_STATE_PATH!" del /f /q "!RECOIL_RECOGNIZER_STATE_PATH!" >nul 2>nul
"%EXE%" --config config.toml --perf-log !AUTO_FIRE_ARG!

:end
endlocal
