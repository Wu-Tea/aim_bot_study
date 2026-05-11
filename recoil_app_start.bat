@echo off
setlocal
cd /d "%~dp0"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3.11"
) else (
    set "PYTHON_CMD=python"
)

if not "%~1"=="" goto passthrough

echo.
echo ==================================
echo Recoil App
echo ==================================
echo.
echo Select game:
echo 1. COD20
echo 2. COD21
echo 3. COD22
if defined RECOIL_APP_GAME_CHOICE_OVERRIDE (
    set "RECOIL_GAME_CHOICE=%RECOIL_APP_GAME_CHOICE_OVERRIDE%"
) else (
    set /p "RECOIL_GAME_CHOICE=Choose [1-3] (default 3): "
)
set "RECOIL_GAME_CHOICE=%RECOIL_GAME_CHOICE: =%"
set "RECOIL_GAME_CHOICE=%RECOIL_GAME_CHOICE:~0,1%"
set "RECOIL_GAME=cod22"
if "%RECOIL_GAME_CHOICE%"=="1" set "RECOIL_GAME=cod20"
if "%RECOIL_GAME_CHOICE%"=="2" set "RECOIL_GAME=cod21"
if "%RECOIL_GAME_CHOICE%"=="3" set "RECOIL_GAME=cod22"

echo.
echo Select mode:
echo 1. Record mode
echo 2. Recoil mode
if defined RECOIL_APP_MODE_CHOICE_OVERRIDE (
    set "RECOIL_MODE_CHOICE=%RECOIL_APP_MODE_CHOICE_OVERRIDE%"
) else (
    set /p "RECOIL_MODE_CHOICE=Choose [1-2] (default 2): "
)
set "RECOIL_MODE_CHOICE=%RECOIL_MODE_CHOICE: =%"
set "RECOIL_MODE_CHOICE=%RECOIL_MODE_CHOICE:~0,1%"
set "RECOIL_MODE=recoil"
if "%RECOIL_MODE_CHOICE%"=="1" set "RECOIL_MODE=record"
if "%RECOIL_MODE_CHOICE%"=="2" set "RECOIL_MODE=recoil"

if "%RECOIL_APP_PRINT_ONLY%"=="1" (
    echo Resolved command: %PYTHON_CMD% -m recoil_app --game %RECOIL_GAME% --mode %RECOIL_MODE%
    goto end
)

%PYTHON_CMD% -m recoil_app --game %RECOIL_GAME% --mode %RECOIL_MODE%
goto end

:passthrough
%PYTHON_CMD% -m recoil_app %*

:end
