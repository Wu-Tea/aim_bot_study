@echo off
setlocal
cd /d "%~dp0..\.."
call "%~dp0gamepad_native_cpp_start.bat" %*
exit /b %ERRORLEVEL%
