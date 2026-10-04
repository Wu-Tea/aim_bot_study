@echo off
setlocal
set "ENABLE_RUNTIME_PERF_LOG=1"
call "%~dp0..\gamepad_native_cpp_start.bat" %*
exit /b %ERRORLEVEL%
