@echo off
setlocal
cd /d "%~dp0..\.."
set "MOUSE_MONITOR_EXE=%CD%\artifacts\mouse_input_monitor\build\Release\mouse_input_monitor.exe"
if not exist "%MOUSE_MONITOR_EXE%" (
  echo Listener is not built. Run scripts\verify\build_mouse_input_monitor.ps1 first.
  pause
  exit /b 1
)
echo Mouse input evidence monitor. Default: passive listening for 30 seconds.
echo Reports: runs\mouse_input_monitor. No driver or AI runtime is started.
"%MOUSE_MONITOR_EXE%" --seconds 30 %*
set "MOUSE_MONITOR_EXIT=%ERRORLEVEL%"
echo Listener exit code: %MOUSE_MONITOR_EXIT%
pause
exit /b %MOUSE_MONITOR_EXIT%
