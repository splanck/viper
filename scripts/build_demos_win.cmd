@echo off
setlocal enabledelayedexpansion

REM Build native x86_64 binaries for all demos using viper project format
REM Usage: scripts\build_demos.cmd [--clean]

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build"
set "BIN_DIR=%ROOT_DIR%\examples\bin"
set "GAMES_DIR=%ROOT_DIR%\examples\games"
set "APPS_DIR=%ROOT_DIR%\examples\apps"

set "VIPER=%BUILD_DIR%\src\tools\viper\Debug\viper.exe"

set CLEAN=0
set FAILED=0
set SUCCEEDED=0

REM Parse arguments
:parse_args
if "%~1"=="" goto :done_args
if "%~1"=="--clean" (
    set CLEAN=1
    shift
    goto :parse_args
)
if "%~1"=="-h" goto :usage
if "%~1"=="--help" goto :usage
echo Unknown argument: %~1
goto :usage

:usage
echo Usage: %~nx0 [--clean]
echo   --clean    Remove existing binaries before building
exit /b 1

:done_args

echo Building Viper demos as native x86_64 binaries
echo ==============================================
echo.
echo Note: larger demos can stay quiet for several minutes while codegen runs.
echo.

REM Check prerequisites
if not exist "%VIPER%" (
    echo ERROR: viper not found at %VIPER%
    echo Run 'cmake --build build' first
    exit /b 1
)

REM Create directories
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

if %CLEAN%==1 (
    echo Cleaning existing binaries...
    del /q "%BIN_DIR%\*" 2>nul
)

echo === BASIC Demos ===
echo.

REM Build BASIC demos
call :build_demo vtris "%GAMES_DIR%\vtris"
call :build_demo frogger "%GAMES_DIR%\frogger-basic"
call :build_demo centipede "%GAMES_DIR%\centipede-basic"

echo === Zia Demos ===
echo.

REM Build Zia demos
call :build_demo paint "%APPS_DIR%\paint"
call :build_demo viperide "%APPS_DIR%\viperide"
call :build_demo 3dbowling "%GAMES_DIR%\3dbowling"
call :build_demo pacman-zia "%GAMES_DIR%\pacman"
call :build_demo vipersql "%APPS_DIR%\vipersql"
call :build_demo chess-zia "%GAMES_DIR%\chess"
call :build_demo xenoscape "%GAMES_DIR%\xenoscape"

echo ==============================================
if %FAILED%==0 (
    echo All %SUCCEEDED% demos built successfully.
    echo.
    echo Binaries are in: %BIN_DIR%
    dir "%BIN_DIR%"
) else (
    echo %FAILED% demo(s^) failed, %SUCCEEDED% succeeded
)

exit /b %FAILED%

REM ============================================
REM Build a demo from its viper project directory
REM ============================================
:build_demo
set "NAME=%~1"
set "PROJECT_DIR=%~2"

echo Building %NAME%...
echo   Started: %DATE% %TIME%

if /i "%NAME%"=="vipersql" (
    echo   Note: vipersql is the slowest demo on Windows and can take around 10 minutes.
)

if not exist "%PROJECT_DIR%\viper.project" (
    echo   ERROR: No viper.project found in %PROJECT_DIR%
    set /a FAILED+=1
    echo   Finished: %DATE% %TIME%
    echo.
    goto :eof
)

set "EXE_FILE=%BIN_DIR%\%NAME%.exe"

echo   Compiling...
"%VIPER%" build "%PROJECT_DIR%" --arch x64 -o "%EXE_FILE%" 2>nul
if errorlevel 1 goto :build_demo_failed
echo   OK
echo   Built: %EXE_FILE%
echo   Finished: %DATE% %TIME%
set /a SUCCEEDED+=1
echo.
goto :eof

:build_demo_failed
echo   FAILED
"%VIPER%" build "%PROJECT_DIR%" --arch x64 -o "%EXE_FILE%" 2>&1
echo   Finished: %DATE% %TIME%
set /a FAILED+=1
echo.
goto :eof
