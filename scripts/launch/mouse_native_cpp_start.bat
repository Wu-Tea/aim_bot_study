@echo off
setlocal
cd /d "%~dp0..\.."
set "EXE=%CD%\native\vision_native\build\Release\cod_native_mouse_runtime.exe"
if not exist "%EXE%" (
  echo Build the mouse runtime with scripts\verify\mouse_native.ps1 first.
  exit /b 1
)
if not defined MOUSE_RUNTIME_CONFIG set "MOUSE_RUNTIME_CONFIG=%CD%\config.toml"
if not defined MOUSE_RUNTIME_TRANSPORT set "MOUSE_RUNTIME_TRANSPORT=virtual-hid"
if not exist "%MOUSE_RUNTIME_CONFIG%" (
  echo Mouse runtime configuration is missing.
  exit /b 1
)
echo Mouse transport: %MOUSE_RUNTIME_TRANSPORT%
echo Device route: Interception capture to independent FakerInput virtual mouse.
echo Default source: verified Razer hardware ID. Use --check-transport or --mouse-hardware ID to select another device.
echo No Win32 fallback. Driver installation is separate; this launcher never installs a driver.
echo Mouse cadence: 1000 Hz target. AI movement uses elapsed time; runtime prints measured rates.
echo Mouse tuning comes from [mouse] in the config: default speed 2x, breakaway 4x, BodyLock deadzone 0.5.
echo Command-line options override the config. Use --check-config to view effective values.
echo Selected physical movement, buttons and wheel are consumed and rewritten once through the virtual mouse.
echo Release all mouse buttons before activation. Independent supervisor owns Ctrl+Alt+F12.
echo Aim at a stationary dummy. Ctrl+Alt+F11 calibrates Hipfire with RMB up, ADS with RMB held.
echo Response settings also come from [mouse]: dpi, sensitivity, fov, ads_multiplier. Calibration is optional.
echo F11 replaces the estimate for that mode in memory. Restart reloads the configured estimate.
echo Emergency release and exit: Ctrl+Alt+F12
if "%MOUSE_START_PRINT_ONLY%"=="1" (
  echo Resolved command: "%EXE%" --config "%MOUSE_RUNTIME_CONFIG%" --transport %MOUSE_RUNTIME_TRANSPORT% %*
  exit /b 0
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0mouse_native_logged_start.ps1" %*
set "MOUSE_RUNTIME_EXIT=%ERRORLEVEL%"
if not "%MOUSE_RUNTIME_EXIT%"=="0" if not "%MOUSE_START_NO_PAUSE%"=="1" pause
exit /b %MOUSE_RUNTIME_EXIT%
