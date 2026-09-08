@echo off
setlocal
set "MOUSE_RUNTIME_TRANSPORT=win32-debug"
call "%~dp0..\mouse_native_cpp_start.bat" %*
exit /b %ERRORLEVEL%
