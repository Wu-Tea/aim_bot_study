@echo off
setlocal
if not defined MOUSE_RUNTIME_TRANSPORT set "MOUSE_RUNTIME_TRANSPORT=virtual-hid"
call "%~dp0..\mouse_native_cpp_start.bat" %*
exit /b %ERRORLEVEL%
