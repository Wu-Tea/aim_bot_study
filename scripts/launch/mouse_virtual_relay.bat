@echo off
setlocal
cd /d "%~dp0\..\.."
set "VIRTUAL_RELAY_EXE=%CD%\artifacts\mouse_link\virtual-build\Release\mouse_virtual_relay.exe"
if not exist "%VIRTUAL_RELAY_EXE%" (
    echo Build first: D:\env\python\python.exe scripts\verify\build_mouse_virtual_relay.py
    pause
    exit /b 2
)
if "%~1"=="" (
    "%VIRTUAL_RELAY_EXE%" --hardware "HID\VID_1532&PID_00B8&REV_0100&MI_00"
) else (
    "%VIRTUAL_RELAY_EXE%" %*
)
set "VIRTUAL_RELAY_EXIT=%ERRORLEVEL%"
echo.
echo Relay exited with code %VIRTUAL_RELAY_EXIT%.
pause
exit /b %VIRTUAL_RELAY_EXIT%
