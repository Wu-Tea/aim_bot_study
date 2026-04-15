@echo off
setlocal
cd /d "%~dp0"

set "VISION_PERF_LOG=1"
set "AUTO_FIRE_OUTPUT=RB"

echo Select AutoFire output:
echo 1. RB
echo 2. RT
set /p "FIRE_CHOICE=Choose [1/2] (default 1): "

if /I "%FIRE_CHOICE%"=="2" (
    set "AUTO_FIRE_OUTPUT=RT"
) else if not "%FIRE_CHOICE%"=="" if /I not "%FIRE_CHOICE%"=="1" (
    echo Invalid selection. Using default: RB
)

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

echo Launching gamepad debug mode with AutoFire=%AUTO_FIRE_OUTPUT%
%PYTHON_CMD% main.py --controller-mode gamepad --auto-fire-output %AUTO_FIRE_OUTPUT% --vision-debug --vision-debug-save
pause
