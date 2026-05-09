@echo off
setlocal enabledelayedexpansion

REM Build native binaries for all demos using viper project format
REM Usage: scripts\build_demos.cmd [--clean] [--arch arm64|x64]

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR_EXPLICIT=0"
set "TOOL_BUILD_DIR_EXPLICIT=0"
if not "%VIPER_DEMO_BUILD_DIR%"=="" (
    set "BUILD_DIR=%VIPER_DEMO_BUILD_DIR%"
    set "BUILD_DIR_EXPLICIT=1"
) else if not "%VIPER_BUILD_DIR%"=="" (
    set "BUILD_DIR=%VIPER_BUILD_DIR%"
    set "BUILD_DIR_EXPLICIT=1"
) else (
    set "BUILD_DIR=%ROOT_DIR%\build"
)
if not "%VIPER_DEMO_TOOL_BUILD_DIR%"=="" (
    set "TOOL_BUILD_DIR=%VIPER_DEMO_TOOL_BUILD_DIR%"
    set "TOOL_BUILD_DIR_EXPLICIT=1"
) else (
    set "TOOL_BUILD_DIR=%ROOT_DIR%\build"
)
set "BIN_DIR=%ROOT_DIR%\examples\bin"
set "GAMES_DIR=%ROOT_DIR%\examples\games"
set "APPS_DIR=%ROOT_DIR%\examples\apps"

if "%VIPER_BUILD_TYPE%"=="" set "VIPER_BUILD_TYPE=Debug"
if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"

set CLEAN=0
set FAILED=0
set SUCCEEDED=0
set "DEMO_ARCH=%VIPER_DEMO_ARCH%"

REM Parse arguments
:parse_args
if "%~1"=="" goto :done_args
if "%~1"=="--clean" (
    set CLEAN=1
    shift
    goto :parse_args
)
if "%~1"=="--arch" (
    if "%~2"=="" (
        echo ERROR: --arch requires arm64 or x64
        exit /b 1
    )
    set "DEMO_ARCH=%~2"
    shift
    shift
    goto :parse_args
)
if "%~1"=="-h" goto :usage
if "%~1"=="--help" goto :usage
echo Unknown argument: %~1
goto :usage

:usage
echo Usage: %~nx0 [--clean] [--arch arm64^|x64]
echo   --clean    Remove existing binaries before building
echo   --arch     Target architecture ^(default: host, or VIPER_DEMO_ARCH^)
exit /b 1

:done_args

if "%DEMO_ARCH%"=="" (
    if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
        set "DEMO_ARCH=arm64"
    ) else (
        set "DEMO_ARCH=x64"
    )
)

if /I "%DEMO_ARCH%"=="aarch64" set "DEMO_ARCH=arm64"
if /I "%DEMO_ARCH%"=="amd64" set "DEMO_ARCH=x64"
if /I "%DEMO_ARCH%"=="x86_64" set "DEMO_ARCH=x64"
if /I "%DEMO_ARCH%"=="arm64" goto :arch_ok
if /I "%DEMO_ARCH%"=="x64" goto :arch_ok
echo ERROR: invalid demo architecture "%DEMO_ARCH%"; expected arm64 or x64
exit /b 1

:arch_ok
set "HOST_DEMO_ARCH=x64"
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOST_DEMO_ARCH=arm64"

if "%BUILD_DIR_EXPLICIT%"=="0" (
    if /I not "%DEMO_ARCH%"=="%HOST_DEMO_ARCH%" (
        set "BUILD_DIR=%ROOT_DIR%\build-%DEMO_ARCH%"
    )
)
if "%TOOL_BUILD_DIR_EXPLICIT%"=="0" (
    if /I "%DEMO_ARCH%"=="%HOST_DEMO_ARCH%" (
        set "TOOL_BUILD_DIR=%BUILD_DIR%"
    )
)

set "VIPER=%TOOL_BUILD_DIR%\src\tools\viper\%VIPER_BUILD_TYPE%\viper.exe"
set "VIPER_BUILD_DIR=%BUILD_DIR%"
set "TARGET_VIPER=%BUILD_DIR%\src\tools\viper\%VIPER_BUILD_TYPE%\viper.exe"

echo Building Viper demos as native %DEMO_ARCH% binaries
echo ==============================================
echo.
echo Note: larger demos can stay quiet for several minutes while codegen runs.
echo Using Viper tool: %TOOL_BUILD_DIR%
echo Using target runtime build: %BUILD_DIR%
echo.

REM Check prerequisites
if not exist "%VIPER%" (
    call :ensure_tool_build
    if errorlevel 1 exit /b 1
)
if /I not "%TOOL_BUILD_DIR%"=="%BUILD_DIR%" (
    if not exist "%TARGET_VIPER%" (
        call :ensure_viper_build
        if errorlevel 1 exit /b 1
    )
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
call :build_demo crackman "%GAMES_DIR%\crackman"
call :build_demo vipersql "%APPS_DIR%\vipersql"
call :build_demo chess-zia "%GAMES_DIR%\chess"
call :build_demo xenoscape "%GAMES_DIR%\xenoscape"
call :build_demo baseball "%GAMES_DIR%\baseball"

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
    echo   Note: vipersql is the slowest demo on Windows and can take several minutes.
)

if not exist "%PROJECT_DIR%\viper.project" (
    echo   ERROR: No viper.project found in %PROJECT_DIR%
    set /a FAILED+=1
    echo   Finished: %DATE% %TIME%
    echo.
    goto :eof
)

set "EXE_FILE=%BIN_DIR%\%NAME%.exe"
set "DEMO_BUILD_FLAGS="

echo   Compiling...
if /i "%NAME%"=="vipersql" (
    set "DEMO_BUILD_FLAGS=-O0"
    echo   Using -O0 to avoid pathological optimizer/codegen time for this large demo.
)
if /i "%NAME%"=="chess-zia" (
    set "DEMO_BUILD_FLAGS=-O0"
    echo   Using -O0 to avoid the optimized Windows x64 chess AI move-value crash.
)
if /i "%NAME%"=="xenoscape" (
    set "DEMO_BUILD_FLAGS=-O0"
    echo   Using -O0 to avoid the optimized Windows x64 new-game startup crash.
)
if /i "%NAME%"=="baseball" (
    set "DEMO_BUILD_FLAGS=--build-profile debug"
    echo   Using debug build profile to avoid the optimized Windows x64 season simulation crash.
)
"%VIPER%" build "%PROJECT_DIR%" --arch %DEMO_ARCH% !DEMO_BUILD_FLAGS! -o "%EXE_FILE%" 2>nul
if errorlevel 1 goto :build_demo_failed
echo   OK
echo   Built: %EXE_FILE%
echo   Finished: %DATE% %TIME%
set /a SUCCEEDED+=1
echo.
goto :eof

:build_demo_failed
echo   FAILED
"%VIPER%" build "%PROJECT_DIR%" --arch %DEMO_ARCH% !DEMO_BUILD_FLAGS! -o "%EXE_FILE%" 2>&1
echo   Finished: %DATE% %TIME%
set /a FAILED+=1
echo.
goto :eof

REM ============================================
REM Ensure a Viper tool/runtime build exists for the requested demo arch.
REM ============================================
:ensure_viper_build
echo Viper tool not found at %VIPER%
echo Configuring/building target-architecture Viper runtime...

set "ARCH_ARG="
if "%BUILD_DIR_EXPLICIT%"=="0" (
    if "%VIPER_CMAKE_GENERATOR%"=="" (
        if /I "%DEMO_ARCH%"=="arm64" set "ARCH_ARG=-A ARM64"
        if /I "%DEMO_ARCH%"=="x64" set "ARCH_ARG=-A x64"
    )
)

set "CONFIG_ARGS="
if not "%VIPER_EXTRA_CMAKE_ARGS%"=="" set "CONFIG_ARGS=%VIPER_EXTRA_CMAKE_ARGS%"

if not "%VIPER_CMAKE_GENERATOR%"=="" (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "%VIPER_CMAKE_GENERATOR%" %ARCH_ARG% -DCMAKE_BUILD_TYPE=%VIPER_BUILD_TYPE% %CONFIG_ARGS%
) else (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" %ARCH_ARG% -DCMAKE_BUILD_TYPE=%VIPER_BUILD_TYPE% %CONFIG_ARGS%
)
if errorlevel 1 (
    echo ERROR: CMake configuration failed for %DEMO_ARCH% build
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config %VIPER_BUILD_TYPE% --target viper -j %JOBS%
if errorlevel 1 (
    echo ERROR: Viper build failed for %DEMO_ARCH%
    exit /b 1
)

if not exist "%VIPER%" (
    echo ERROR: viper still not found at %VIPER%
    exit /b 1
)

exit /b 0

REM ============================================
REM Ensure a host-native Viper tool exists.
REM ============================================
:ensure_tool_build
echo Host Viper tool not found at %VIPER%
echo Configuring/building host-native Viper tool...

set "TOOL_ARCH_ARG="
if "%TOOL_BUILD_DIR_EXPLICIT%"=="0" (
    if "%VIPER_CMAKE_GENERATOR%"=="" (
        if /I "%HOST_DEMO_ARCH%"=="arm64" set "TOOL_ARCH_ARG=-A ARM64"
        if /I "%HOST_DEMO_ARCH%"=="x64" set "TOOL_ARCH_ARG=-A x64"
    )
)

set "CONFIG_ARGS="
if not "%VIPER_EXTRA_CMAKE_ARGS%"=="" set "CONFIG_ARGS=%VIPER_EXTRA_CMAKE_ARGS%"

if not "%VIPER_CMAKE_GENERATOR%"=="" (
    cmake -S "%ROOT_DIR%" -B "%TOOL_BUILD_DIR%" -G "%VIPER_CMAKE_GENERATOR%" %TOOL_ARCH_ARG% -DCMAKE_BUILD_TYPE=%VIPER_BUILD_TYPE% %CONFIG_ARGS%
) else (
    cmake -S "%ROOT_DIR%" -B "%TOOL_BUILD_DIR%" %TOOL_ARCH_ARG% -DCMAKE_BUILD_TYPE=%VIPER_BUILD_TYPE% %CONFIG_ARGS%
)
if errorlevel 1 (
    echo ERROR: CMake configuration failed for host Viper tool build
    exit /b 1
)

cmake --build "%TOOL_BUILD_DIR%" --config %VIPER_BUILD_TYPE% --target viper -j %JOBS%
if errorlevel 1 (
    echo ERROR: host Viper tool build failed
    exit /b 1
)

if not exist "%VIPER%" (
    echo ERROR: host Viper tool still not found at %VIPER%
    exit /b 1
)

exit /b 0
