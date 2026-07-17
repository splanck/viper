@echo off
setlocal enabledelayedexpansion

REM Build ZannaIDE as a standalone native Windows binary.
REM Usage: scripts\build_ide_win.cmd [--clean] [--arch arm64|x64] [--output PATH]

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR_EXPLICIT=0"
set "TOOL_BUILD_DIR_EXPLICIT=0"
if not "%ZANNA_IDE_BUILD_DIR%"=="" (
    set "BUILD_DIR=%ZANNA_IDE_BUILD_DIR%"
    set "BUILD_DIR_EXPLICIT=1"
) else if not "%ZANNA_BUILD_DIR%"=="" (
    set "BUILD_DIR=%ZANNA_BUILD_DIR%"
    set "BUILD_DIR_EXPLICIT=1"
) else (
    set "BUILD_DIR=%ROOT_DIR%\build"
)
if not "%ZANNA_IDE_TOOL_BUILD_DIR%"=="" (
    set "TOOL_BUILD_DIR=%ZANNA_IDE_TOOL_BUILD_DIR%"
    set "TOOL_BUILD_DIR_EXPLICIT=1"
) else (
    set "TOOL_BUILD_DIR=%ROOT_DIR%\build"
)
set "IDE_DIR=%ROOT_DIR%\zannaide"
set "IDE_BIN_DIR=%IDE_DIR%\bin"
if not "%ZANNA_IDE_OUT_DIR%"=="" set "IDE_BIN_DIR=%ZANNA_IDE_OUT_DIR%"
if not "%ZANNA_IDE_OUTPUT%"=="" (
    set "OUTPUT_FILE=%ZANNA_IDE_OUTPUT%"
) else (
    set "OUTPUT_FILE=%IDE_BIN_DIR%\zannaide.exe"
)
if "%ZANNA_IDE_COMPAT_OUTPUT%"=="" (
    set "COMPAT_OUTPUT_FILE=%BUILD_DIR%\zannaide\zannaide.exe"
) else (
    set "COMPAT_OUTPUT_FILE=%ZANNA_IDE_COMPAT_OUTPUT%"
)
if "%ZANNA_IDE_SKIP_COMPAT_COPY%"=="" set "ZANNA_IDE_SKIP_COMPAT_COPY=0"

if "%ZANNA_BUILD_TYPE%"=="" set "ZANNA_BUILD_TYPE=Release"
if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"

set CLEAN=0
set "IDE_ARCH=%ZANNA_IDE_ARCH%"
if "%IDE_ARCH%"=="" set "IDE_ARCH=%ZANNA_DEMO_ARCH%"

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
echo   --clean        Remove the existing ZannaIDE binary before building
echo   --arch         Target architecture ^(default: host, or ZANNA_IDE_ARCH^)
echo   --output PATH  Write the binary to PATH ^(default: zannaide\bin\zannaide.exe^)
echo   Compatibility copy: build\zannaide\zannaide.exe unless ZANNA_IDE_SKIP_COMPAT_COPY=1
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

set "ZANNA=%TOOL_BUILD_DIR%\src\tools\zanna\%ZANNA_BUILD_TYPE%\zanna.exe"
set "ZANNA_BUILD_DIR=%BUILD_DIR%"
set "TARGET_ZANNA=%BUILD_DIR%\src\tools\zanna\%ZANNA_BUILD_TYPE%\zanna.exe"

echo Building ZannaIDE as a native %IDE_ARCH% binary
echo ==============================================
echo.
echo Using Zanna tool: %TOOL_BUILD_DIR%
echo Using target runtime build: %BUILD_DIR%
echo Source: %IDE_DIR%
echo Output: %OUTPUT_FILE%
echo.

if not exist "%IDE_DIR%\zanna.project" (
    echo ERROR: No zanna.project found in %IDE_DIR%
    exit /b 1
)

REM Check prerequisites
if not exist "%ZANNA%" (
    call :ensure_tool_build
    if errorlevel 1 exit /b 1
)
if /I not "%TOOL_BUILD_DIR%"=="%BUILD_DIR%" (
    if not exist "%TARGET_ZANNA%" (
        call :ensure_zanna_build
        if errorlevel 1 exit /b 1
    )
)

for %%I in ("%OUTPUT_FILE%") do set "OUTPUT_DIR=%%~dpI"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

if %CLEAN%==1 (
    echo Cleaning existing ZannaIDE binary...
    del /q "%OUTPUT_FILE%" 2>nul
    del /q "%OUTPUT_DIR%zannaide.buildinfo" 2>nul
    if /I not "%OUTPUT_FILE%"=="%COMPAT_OUTPUT_FILE%" (
        del /q "%COMPAT_OUTPUT_FILE%" 2>nul
        for %%I in ("%COMPAT_OUTPUT_FILE%") do del /q "%%~dpIzannaide.buildinfo" 2>nul
    )
)

echo Compiling...
"%ZANNA%" build "%IDE_DIR%" --arch %IDE_ARCH% -o "%OUTPUT_FILE%" 2>nul
if errorlevel 1 goto :build_failed

echo OK
call :write_build_info "%OUTPUT_FILE%"
call :mirror_compat_output
echo Built: %OUTPUT_FILE%
echo Build info: %OUTPUT_DIR%zannaide.buildinfo
exit /b 0

:build_failed
echo FAILED
"%ZANNA%" build "%IDE_DIR%" --arch %IDE_ARCH% -o "%OUTPUT_FILE%" 2>&1
exit /b 1

:write_build_info
set "BUILD_INFO_BINARY=%~1"
for %%I in ("%BUILD_INFO_BINARY%") do set "BUILD_INFO_PATH=%%~dpIzannaide.buildinfo"
set "BUILD_REVISION=unknown"
for /f "usebackq delims=" %%G in (`git -C "%ROOT_DIR%" rev-parse --short HEAD 2^>nul`) do set "BUILD_REVISION=%%G"
git -C "%ROOT_DIR%" diff --quiet --ignore-submodules -- 2>nul
if errorlevel 1 (
    set "BUILD_DIRTY= dirty"
) else (
    set "BUILD_DIRTY="
)
> "%BUILD_INFO_PATH%" echo Build: %DATE% %TIME%
>>"%BUILD_INFO_PATH%" echo Source: %BUILD_REVISION%%BUILD_DIRTY%
>>"%BUILD_INFO_PATH%" echo Output: %BUILD_INFO_BINARY%
>>"%BUILD_INFO_PATH%" echo Zanna: %ZANNA%
exit /b 0

:mirror_compat_output
if "%ZANNA_IDE_SKIP_COMPAT_COPY%"=="1" exit /b 0
if /I "%OUTPUT_FILE%"=="%COMPAT_OUTPUT_FILE%" exit /b 0
for %%I in ("%COMPAT_OUTPUT_FILE%") do set "COMPAT_OUTPUT_DIR=%%~dpI"
if not exist "%COMPAT_OUTPUT_DIR%" mkdir "%COMPAT_OUTPUT_DIR%"
copy /y "%OUTPUT_FILE%" "%COMPAT_OUTPUT_FILE%" >nul
if errorlevel 1 exit /b 1
call :write_build_info "%COMPAT_OUTPUT_FILE%"
echo Compatibility copy: %COMPAT_OUTPUT_FILE%
exit /b 0

REM ============================================
REM Ensure a Zanna tool/runtime build exists for the requested IDE arch.
REM ============================================
:ensure_zanna_build
echo Target Zanna tool not found at %TARGET_ZANNA%
echo Configuring/building target-architecture Zanna runtime...

set "ARCH_ARG="
if "%BUILD_DIR_EXPLICIT%"=="0" (
    if "%ZANNA_CMAKE_GENERATOR%"=="" (
        if /I "%IDE_ARCH%"=="arm64" set "ARCH_ARG=-A ARM64"
        if /I "%IDE_ARCH%"=="x64" set "ARCH_ARG=-A x64"
    )
)

set "CONFIG_ARGS="
if not "%ZANNA_EXTRA_CMAKE_ARGS%"=="" set "CONFIG_ARGS=%ZANNA_EXTRA_CMAKE_ARGS%"

if not "%ZANNA_CMAKE_GENERATOR%"=="" (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "%ZANNA_CMAKE_GENERATOR%" %ARCH_ARG% -DCMAKE_BUILD_TYPE=%ZANNA_BUILD_TYPE% %CONFIG_ARGS%
) else (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" %ARCH_ARG% -DCMAKE_BUILD_TYPE=%ZANNA_BUILD_TYPE% %CONFIG_ARGS%
)
if errorlevel 1 (
    echo ERROR: CMake configuration failed for %IDE_ARCH% build
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config %ZANNA_BUILD_TYPE% --target zanna -j %JOBS%
if errorlevel 1 (
    echo ERROR: Zanna build failed for %IDE_ARCH%
    exit /b 1
)

if not exist "%TARGET_ZANNA%" (
    echo ERROR: target Zanna tool still not found at %TARGET_ZANNA%
    exit /b 1
)

exit /b 0

REM ============================================
REM Ensure a host-native Zanna tool exists.
REM ============================================
:ensure_tool_build
echo Host Zanna tool not found at %ZANNA%
echo Configuring/building host-native Zanna tool...

set "TOOL_ARCH_ARG="
if "%TOOL_BUILD_DIR_EXPLICIT%"=="0" (
    if "%ZANNA_CMAKE_GENERATOR%"=="" (
        if /I "%HOST_IDE_ARCH%"=="arm64" set "TOOL_ARCH_ARG=-A ARM64"
        if /I "%HOST_IDE_ARCH%"=="x64" set "TOOL_ARCH_ARG=-A x64"
    )
)

set "CONFIG_ARGS="
if not "%ZANNA_EXTRA_CMAKE_ARGS%"=="" set "CONFIG_ARGS=%ZANNA_EXTRA_CMAKE_ARGS%"

if not "%ZANNA_CMAKE_GENERATOR%"=="" (
    cmake -S "%ROOT_DIR%" -B "%TOOL_BUILD_DIR%" -G "%ZANNA_CMAKE_GENERATOR%" %TOOL_ARCH_ARG% -DCMAKE_BUILD_TYPE=%ZANNA_BUILD_TYPE% %CONFIG_ARGS%
) else (
    cmake -S "%ROOT_DIR%" -B "%TOOL_BUILD_DIR%" %TOOL_ARCH_ARG% -DCMAKE_BUILD_TYPE=%ZANNA_BUILD_TYPE% %CONFIG_ARGS%
)
if errorlevel 1 (
    echo ERROR: CMake configuration failed for host Zanna tool build
    exit /b 1
)

cmake --build "%TOOL_BUILD_DIR%" --config %ZANNA_BUILD_TYPE% --target zanna -j %JOBS%
if errorlevel 1 (
    echo ERROR: host Zanna tool build failed
    exit /b 1
)

if not exist "%ZANNA%" (
    echo ERROR: host Zanna tool still not found at %ZANNA%
    exit /b 1
)

exit /b 0
