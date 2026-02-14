@echo off
setlocal enabledelayedexpansion

REM Build native x86_64 binaries for all demos using viper project format
REM Usage: scripts\build_demos.cmd [--clean]

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build"
set "BIN_DIR=%ROOT_DIR%\demos\bin"
set "BASIC_DIR=%ROOT_DIR%\demos\basic"
set "ZIA_DIR=%ROOT_DIR%\demos\zia"

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
call :build_demo chess "%BASIC_DIR%\chess"
call :build_demo vtris "%BASIC_DIR%\vtris"
call :build_demo frogger "%BASIC_DIR%\frogger"
call :build_demo centipede "%BASIC_DIR%\centipede"
call :build_demo pacman "%BASIC_DIR%\pacman"

echo === Zia Demos ===
echo.

REM Build Zia demos
call :build_demo paint "%ZIA_DIR%\paint"
REM viperide requires unimplemented vipergfx GUI widget layer (vg_* symbols)
REM call :build_demo viperide "%ZIA_DIR%\viperide"
call :build_demo pacman-zia "%ZIA_DIR%\pacman"
call :build_demo sqldb "%ZIA_DIR%\sqldb"

echo ==============================================
if %FAILED%==0 (
    echo All %SUCCEEDED% demos built successfully!
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

if not exist "%PROJECT_DIR%\viper.project" (
    echo   ERROR: No viper.project found in %PROJECT_DIR%
    set /a FAILED+=1
    echo.
    goto :eof
)

set "EXE_FILE=%BIN_DIR%\%NAME%.exe"

echo   Compiling...
"%VIPER%" build "%PROJECT_DIR%" --arch x64 -o "%EXE_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED
    "%VIPER%" build "%PROJECT_DIR%" --arch x64 -o "%EXE_FILE%" 2>&1
    set /a FAILED+=1
    echo.
    goto :eof
)
echo   OK

echo   Built: %EXE_FILE%
set /a SUCCEEDED+=1
echo.
goto :eof
