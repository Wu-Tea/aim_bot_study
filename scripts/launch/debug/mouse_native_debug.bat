@echo off
setlocal
cd /d "%~dp0..\..\.."

set "VISION_PERF_LOG=1"
set "VISION_BACKEND=native"
set "MOUSE_TELEMETRY=1"
if not defined MOUSE_INJECTION_BACKEND set "MOUSE_INJECTION_BACKEND=sendinput"
if not defined VISION_CAPTURE_FPS set "VISION_CAPTURE_FPS=140"
if not defined VISION_QUIT_KEY set "VISION_QUIT_KEY=Q"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

for /f %%I in ('%PYTHON_CMD% -c "from datetime import datetime; print(datetime.now().strftime(r'artifacts\mouse_telemetry\mouse-controller-%%Y%%m%%d-%%H%%M%%S-debug.csv'))"') do set "MOUSE_TELEMETRY_PATH=%%I"

echo Vision settings: backend=%VISION_BACKEND% capture_fps=%VISION_CAPTURE_FPS% quit_key=%VISION_QUIT_KEY% debug=on debug_save=on mouse_telemetry=%MOUSE_TELEMETRY% mouse_input=%MOUSE_INJECTION_BACKEND%
echo Mouse telemetry path: %MOUSE_TELEMETRY_PATH%
if not "%MOUSE_PROBE_INPUT%"=="0" (
    echo Mouse injection probe:
    %PYTHON_CMD% tools\probe_mouse_injection.py --backend %MOUSE_INJECTION_BACKEND% --dx 16 --dy 0 --settle 0.050
    if errorlevel 1 (
        echo Mouse injection probe failed. The selected backend is not moving the Windows cursor.
        echo Set MOUSE_PROBE_INPUT=0 to bypass this check if you intentionally want to continue.
        pause
        exit /b 1
    )
)
echo Launching native mouse debug mode
%PYTHON_CMD% main.py --controller-mode mouse --vision-backend %VISION_BACKEND% --vision-debug --vision-debug-save --perf-log

echo.
echo Mouse telemetry summary:
if exist "%MOUSE_TELEMETRY_PATH%" (
    %PYTHON_CMD% tools\analyze_mouse_telemetry.py "%MOUSE_TELEMETRY_PATH%" --assert-healthy
) else (
    echo No mouse telemetry CSV found for this run: %MOUSE_TELEMETRY_PATH%
)
pause
