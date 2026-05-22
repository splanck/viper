@echo off
setlocal enabledelayedexpansion

REM Build ViperIDE as a standalone native Windows binary.
REM Usage: scripts\build_ide_win.cmd [--clean] [--arch arm64|x64] [--output PATH]

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR_EXPLICIT=0"
set "TOOL_BUILD_DIR_EXPLICIT=0"
if not "%VIPER_IDE_BUILD_DIR%"=="" (
    set "BUILD_DIR=%VIPER_IDE_BUILD_DIR%"
    set "BUILD_DIR_EXPLICIT=1"
) else if not "%VIPER_BUILD_DIR%"=="" (
    set "BUILD_DIR=%VIPER_BUILD_DIR%"
    set "BUILD_DIR_EXPLICIT=1"
) else (
    set "BUILD_DIR=%ROOT_DIR%\build"
)
if not "%VIPER_IDE_TOOL_BUILD_DIR%"=="" (
    set "TOOL_BUILD_DIR=%VIPER_IDE_TOOL_BUILD_DIR%"
    set "TOOL_BUILD_DIR_EXPLICIT=1"
) else (
    set "TOOL_BUILD_DIR=%ROOT_DIR%\build"
)
set "IDE_DIR=%ROOT_DIR%\viperide"
set "IDE_BIN_DIR=%IDE_DIR%\bin"
if not "%VIPER_IDE_OUT_DIR%"=="" set "IDE_BIN_DIR=%VIPER_IDE_OUT_DIR%"
if not "%VIPER_IDE_OUTPUT%"=="" (
    set "OUTPUT_FILE=%VIPER_IDE_OUTPUT%"
) else (
    set "OUTPUT_FILE=%IDE_BIN_DIR%\viperide.exe"
)

if "%VIPER_BUILD_TYPE%"=="" set "VIPER_BUILD_TYPE=Debug"
if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"

set CLEAN=0
set "IDE_ARCH=%VIPER_IDE_ARCH%"
if "%IDE_ARCH%"=="" set "IDE_ARCH=%VIPER_DEMO_ARCH%"

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
    set "IDE_ARCH=%~2"
    shift
    shift
    goto :parse_args
)
if "%~1"=="--output" (
    if "%~2"=="" (
        echo ERROR: --output requires a path
        exit /b 1
    )
    set "OUTPUT_FILE=%~2"
    shift
    shift
    goto :parse_args
)
if "%~1"=="-h" goto :usage
if "%~1"=="--help" goto :usage
echo Unknown argument: %~1
goto :usage

:usage
echo Usage: %~nx0 [--clean] [--arch arm64^|x64] [--output PATH]
echo   --clean        Remove the existing ViperIDE binary before building
echo   --arch         Target architecture ^(default: host, or VIPER_IDE_ARCH^)
echo   --output PATH  Write the binary to PATH ^(default: viperide\bin\viperide.exe^)
exit /b 1

:done_args

if "%IDE_ARCH%"=="" (
    if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
        set "IDE_ARCH=arm64"
    ) else (
        set "IDE_ARCH=x64"
    )
)

if /I "%IDE_ARCH%"=="aarch64" set "IDE_ARCH=arm64"
if /I "%IDE_ARCH%"=="amd64" set "IDE_ARCH=x64"
if /I "%IDE_ARCH%"=="x86_64" set "IDE_ARCH=x64"
if /I "%IDE_ARCH%"=="arm64" goto :arch_ok
if /I "%IDE_ARCH%"=="x64" goto :arch_ok
echo ERROR: invalid IDE architecture "%IDE_ARCH%"; expected arm64 or x64
exit /b 1

:arch_ok
set "HOST_IDE_ARCH=x64"
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOST_IDE_ARCH=arm64"

if "%BUILD_DIR_EXPLICIT%"=="0" (
    if /I not "%IDE_ARCH%"=="%HOST_IDE_ARCH%" (
        set "BUILD_DIR=%ROOT_DIR%\build-%IDE_ARCH%"
    )
)
if "%TOOL_BUILD_DIR_EXPLICIT%"=="0" (
    if /I "%IDE_ARCH%"=="%HOST_IDE_ARCH%" (
        set "TOOL_BUILD_DIR=%BUILD_DIR%"
    )
)

set "VIPER=%TOOL_BUILD_DIR%\src\tools\viper\%VIPER_BUILD_TYPE%\viper.exe"
set "VIPER_BUILD_DIR=%BUILD_DIR%"
set "TARGET_VIPER=%BUILD_DIR%\src\tools\viper\%VIPER_BUILD_TYPE%\viper.exe"

echo Building ViperIDE as a native %IDE_ARCH% binary
echo ==============================================
echo.
echo Using Viper tool: %TOOL_BUILD_DIR%
echo Using target runtime build: %BUILD_DIR%
echo Source: %IDE_DIR%
echo Output: %OUTPUT_FILE%
echo.

if not exist "%IDE_DIR%\viper.project" (
    echo ERROR: No viper.project found in %IDE_DIR%
    exit /b 1
)

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

for %%I in ("%OUTPUT_FILE%") do set "OUTPUT_DIR=%%~dpI"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

if %CLEAN%==1 (
    echo Cleaning existing ViperIDE binary...
    del /q "%OUTPUT_FILE%" 2>nul
)

echo Compiling...
"%VIPER%" build "%IDE_DIR%" --arch %IDE_ARCH% -o "%OUTPUT_FILE%" 2>nul
if errorlevel 1 goto :build_failed

echo OK
echo Built: %OUTPUT_FILE%
exit /b 0

:build_failed
echo FAILED
"%VIPER%" build "%IDE_DIR%" --arch %IDE_ARCH% -o "%OUTPUT_FILE%" 2>&1
exit /b 1

REM ============================================
REM Ensure a Viper tool/runtime build exists for the requested IDE arch.
REM ============================================
:ensure_viper_build
echo Target Viper tool not found at %TARGET_VIPER%
echo Configuring/building target-architecture Viper runtime...

set "ARCH_ARG="
if "%BUILD_DIR_EXPLICIT%"=="0" (
    if "%VIPER_CMAKE_GENERATOR%"=="" (
        if /I "%IDE_ARCH%"=="arm64" set "ARCH_ARG=-A ARM64"
        if /I "%IDE_ARCH%"=="x64" set "ARCH_ARG=-A x64"
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
    echo ERROR: CMake configuration failed for %IDE_ARCH% build
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config %VIPER_BUILD_TYPE% --target viper -j %JOBS%
if errorlevel 1 (
    echo ERROR: Viper build failed for %IDE_ARCH%
    exit /b 1
)

if not exist "%TARGET_VIPER%" (
    echo ERROR: target Viper tool still not found at %TARGET_VIPER%
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
        if /I "%HOST_IDE_ARCH%"=="arm64" set "TOOL_ARCH_ARG=-A ARM64"
        if /I "%HOST_IDE_ARCH%"=="x64" set "TOOL_ARCH_ARG=-A x64"
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
