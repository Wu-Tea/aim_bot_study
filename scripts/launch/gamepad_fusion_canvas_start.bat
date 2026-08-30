@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0..\.."

set "RESULT=0"
set "RUNTIME_EXE=native\vision_native\build\Release\cod_native_runtime.exe"
set "CANVAS_EXE=native\vision_native\build\Release\fusion_canvas.exe"

if not exist "%RUNTIME_EXE%" (
  echo Native C++ runtime not found. Run tools\build_native_vision.ps1 first.
  exit /b 1
)

if not exist "%CANVAS_EXE%" (
  echo Fusion canvas not found. Build the fusion_canvas target first.
  echo Expected: %CANVAS_EXE%
  exit /b 1
)

if /I "%FUSION_FORCE_OFF%"=="1" (
  echo FUSION_FORCE_OFF=1 is set. Unset it before using the fusion canvas launcher.
  exit /b 1
)

if not defined FUSION_SESSION set "FUSION_SESSION=dev"
if not defined FUSION_MAX_FPS set "FUSION_MAX_FPS=30"
if not defined FUSION_SHOW_ALL_DETECTIONS set "FUSION_SHOW_ALL_DETECTIONS=0"
if not defined FUSION_IDLE_MODE set "FUSION_IDLE_MODE=hide"
if not defined FUSION_CANVAS_LOG_DIR set "FUSION_CANVAS_LOG_DIR=runs\fusion_canvas"
if not defined FUSION_CANVAS_LOG_FILE set "FUSION_CANVAS_LOG_FILE=!FUSION_CANVAS_LOG_DIR!\fusion_canvas.log"

set "FUSION_ENABLED=1"

if not exist "!FUSION_CANVAS_LOG_DIR!" mkdir "!FUSION_CANVAS_LOG_DIR!" >nul 2>nul

echo Fusion canvas settings:
echo   session: !FUSION_SESSION!
echo   max_fps: !FUSION_MAX_FPS!
echo   show_all_detections: !FUSION_SHOW_ALL_DETECTIONS!
echo   idle_mode: !FUSION_IDLE_MODE!
echo   log_file: !FUSION_CANVAS_LOG_FILE!
echo.
echo Canvas hotkeys:
echo   Ctrl+Shift+F10  toggle canvas visibility
echo   Ctrl+Shift+F11  close canvas
echo.

if "%GAMEPAD_START_PRINT_ONLY%"=="1" (
  echo Resolved capture-isolation preflight: "%CANVAS_EXE%" --verify-capture-isolation --log-file "!FUSION_CANVAS_LOG_FILE!"
  echo Resolved canvas command: "%CANVAS_EXE%" --session !FUSION_SESSION! --max-fps !FUSION_MAX_FPS! --idle-mode !FUSION_IDLE_MODE! --log-file "!FUSION_CANVAS_LOG_FILE!"
  echo Resolved runtime launcher: "%~dp0gamepad_native_cpp_start.bat"
  echo Environment: FUSION_ENABLED=1 FUSION_SESSION=!FUSION_SESSION! FUSION_SHOW_ALL_DETECTIONS=!FUSION_SHOW_ALL_DETECTIONS! FUSION_IDLE_MODE=!FUSION_IDLE_MODE!
  goto end_success
)

echo Verifying that the production DXGI capture excludes Fusion...
"%CANVAS_EXE%" --verify-capture-isolation --log-file "!FUSION_CANVAS_LOG_FILE!"
set "PROBE_RESULT=!ERRORLEVEL!"
if not "!PROBE_RESULT!"=="0" (
  echo Fusion capture-isolation preflight failed with code !PROBE_RESULT!.
  echo The canvas and runtime were not started. See !FUSION_CANVAS_LOG_FILE!.
  exit /b !PROBE_RESULT!
)

start "Fusion Canvas" "%CANVAS_EXE%" --session !FUSION_SESSION! --max-fps !FUSION_MAX_FPS! --idle-mode !FUSION_IDLE_MODE! --log-file "!FUSION_CANVAS_LOG_FILE!"
call "%~dp0gamepad_native_cpp_start.bat"
set "RESULT=!ERRORLEVEL!"
goto finish

:end_success
set "RESULT=0"

:finish
exit /b !RESULT!
