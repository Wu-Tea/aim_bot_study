@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0..\.."

set "EXE=native\build\Release\cod_native_runtime.exe"
if not exist "%EXE%" (
  echo Native C++ runtime not found. Run tools\build_native_runtime.ps1 first.
  exit /b 1
)

rem TODO: Move --perf-log console output off the 1 ms controller thread before
rem enabling it by default again. Synchronous console writes can hold the last
rem ViGEm report for hundreds of milliseconds.
set "PERF_LOG_ARG="
if /I "%ENABLE_RUNTIME_PERF_LOG%"=="1" set "PERF_LOG_ARG=--perf-log"

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

echo Fixed recoil settings: [gamepad.recoil] in config.toml.

if "%GAMEPAD_START_PRINT_ONLY%"=="1" (
  echo Resolved command: "%EXE%" --config config.toml !PERF_LOG_ARG! !AUTO_FIRE_ARG!
  goto end
)

"%EXE%" --config config.toml !PERF_LOG_ARG! !AUTO_FIRE_ARG!

:end
endlocal
