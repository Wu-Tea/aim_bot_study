@echo off
setlocal
cd /d "%~dp0..\.."

powershell -ExecutionPolicy Bypass -File "%~dp0native_pipeline_contract.ps1" %*
exit /b %ERRORLEVEL%
