@echo off
setlocal
cd /d "%~dp0..\.."
set "EXE=%CD%\artifacts\mouse_link\build\Release\mouse_link_tool.exe"
if not exist "%EXE%" (
    echo Build first: pwsh -File scripts\verify\build_mouse_link.ps1
    exit /b 1
)
"%EXE%" %*
exit /b %ERRORLEVEL%
