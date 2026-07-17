@echo off
setlocal enabledelayedexpansion

REM Build curated Zia showcase binaries using zanna project format
REM Usage: scripts\build_demos_win.cmd [--clean] [--arch arm64|x64]

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR_EXPLICIT=0"
set "TOOL_BUILD_DIR_EXPLICIT=0"
if not "%ZANNA_DEMO_BUILD_DIR%"=="" (
    set "BUILD_DIR=%ZANNA_DEMO_BUILD_DIR%"
    set "BUILD_DIR_EXPLICIT=1"
) else if not "%ZANNA_BUILD_DIR%"=="" (
    set "BUILD_DIR=%ZANNA_BUILD_DIR%"
    set "BUILD_DIR_EXPLICIT=1"
) else (
    set "BUILD_DIR=%ROOT_DIR%\build"
)
if not "%ZANNA_DEMO_TOOL_BUILD_DIR%"=="" (
    set "TOOL_BUILD_DIR=%ZANNA_DEMO_TOOL_BUILD_DIR%"
    set "TOOL_BUILD_DIR_EXPLICIT=1"
) else (
    set "TOOL_BUILD_DIR=%ROOT_DIR%\build"
)
set "BIN_DIR=%ROOT_DIR%\examples\bin"
set "DEMO_MANIFEST=%SCRIPT_DIR%demo_projects.list"

if "%ZANNA_BUILD_TYPE%"=="" set "ZANNA_BUILD_TYPE=Debug"
if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"

set CLEAN=0
set FAILED=0
set SUCCEEDED=0
set "DEMO_ARCH=%ZANNA_DEMO_ARCH%"

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
echo   --arch     Target architecture ^(default: host, or ZANNA_DEMO_ARCH^)
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

set "ZANNA=%TOOL_BUILD_DIR%\src\tools\zanna\%ZANNA_BUILD_TYPE%\zanna.exe"
set "ZANNA_BUILD_DIR=%BUILD_DIR%"
set "TARGET_ZANNA=%BUILD_DIR%\src\tools\zanna\%ZANNA_BUILD_TYPE%\zanna.exe"

echo Building Zanna demos as native %DEMO_ARCH% binaries
echo ==============================================
echo.
echo Note: larger demos can stay quiet for several minutes while codegen runs.
echo Using Zanna tool: %TOOL_BUILD_DIR%
echo Using target runtime build: %BUILD_DIR%
echo.

REM Check prerequisites
if not exist "%DEMO_MANIFEST%" (
    echo ERROR: demo manifest not found: %DEMO_MANIFEST%
    exit /b 1
)
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

REM Create directories
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

if %CLEAN%==1 (
    echo Cleaning existing binaries...
    del /q "%BIN_DIR%\*" 2>nul
    for /d %%D in ("%BIN_DIR%\*") do rmdir /s /q "%%~fD" 2>nul
)

echo === Zia Showcase Demos ===
echo.

REM Build every project in the shared showcase manifest.
set MANIFEST_ENTRIES=0
for /f "usebackq eol=# tokens=1-3 delims=|" %%A in ("%DEMO_MANIFEST%") do (
    set /a MANIFEST_ENTRIES+=1
    if "%%C"=="" (
        echo ERROR: invalid demo manifest entry for "%%A"
        set /a FAILED+=1
    ) else if /I "%%B"=="games" (
        call :build_demo "%%A" "%ROOT_DIR%\examples\games\%%C"
    ) else if /I "%%B"=="apps" (
        call :build_demo "%%A" "%ROOT_DIR%\examples\apps\%%C"
    ) else (
        echo ERROR: invalid demo category "%%B" for "%%A"
        set /a FAILED+=1
    )
)
if %MANIFEST_ENTRIES%==0 (
    echo ERROR: demo manifest contains no projects
    exit /b 1
)

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
REM Build a demo from its zanna project directory
REM ============================================
:build_demo
set "NAME=%~1"
set "PROJECT_DIR=%~2"

echo Building %NAME%...
echo   Started: %DATE% %TIME%

if /i "%NAME%"=="zannasql" (
    echo   Note: zannasql is the slowest demo on Windows and can take several minutes.
)

if not exist "%PROJECT_DIR%\zanna.project" (
    echo   ERROR: No zanna.project found in %PROJECT_DIR%
    set /a FAILED+=1
    echo   Finished: %DATE% %TIME%
    echo.
    goto :eof
)

set "EXE_FILE=%BIN_DIR%\%NAME%.exe"
set "DEMO_BUILD_FLAGS="

echo   Compiling...
if /i "%NAME%"=="zannasql" (
    set "DEMO_BUILD_FLAGS=-O0"
    echo   Using -O0 to avoid pathological optimizer/codegen time for this large demo.
)
if /i "%NAME%"=="xenoscape" (
    set "DEMO_BUILD_FLAGS=-O0"
    echo   Using -O0 to avoid the Windows x64 checked-integer optimizer miscompile.
)
"%ZANNA%" build "%PROJECT_DIR%" --arch %DEMO_ARCH% !DEMO_BUILD_FLAGS! -o "%EXE_FILE%" 2>nul
if errorlevel 1 goto :build_demo_failed
call :stage_demo_assets "%PROJECT_DIR%"
if errorlevel 1 goto :build_demo_asset_failed
echo   OK
echo   Built: %EXE_FILE%
echo   Finished: %DATE% %TIME%
set /a SUCCEEDED+=1
echo.
goto :eof

:build_demo_failed
echo   FAILED
"%ZANNA%" build "%PROJECT_DIR%" --arch %DEMO_ARCH% !DEMO_BUILD_FLAGS! -o "%EXE_FILE%" 2>&1
echo   Finished: %DATE% %TIME%
set /a FAILED+=1
echo.
goto :eof

:build_demo_asset_failed
echo   FAILED
echo   Finished: %DATE% %TIME%
set /a FAILED+=1
echo.
goto :eof

REM ============================================
REM Stage assets declared in a demo's zanna.project.
REM ============================================
:stage_demo_assets
set "ASSET_PROJECT_DIR=%~1"
if not exist "%ASSET_PROJECT_DIR%\zanna.project" exit /b 0

for /f "usebackq tokens=1,2,*" %%A in ("%ASSET_PROJECT_DIR%\zanna.project") do (
    if /I "%%A"=="asset" (
        call :stage_demo_asset "%ASSET_PROJECT_DIR%" "%%~B" "%%~C"
        if errorlevel 1 exit /b 1
    )
)
exit /b 0

:stage_demo_asset
set "ASSET_PROJECT_DIR=%~1"
set "ASSET_SOURCE_REL=%~2"
set "ASSET_TARGET_REL=%~3"
if "%ASSET_SOURCE_REL%"=="" exit /b 0
if "%ASSET_TARGET_REL%"=="" set "ASSET_TARGET_REL=."

set "ASSET_SOURCE=%ASSET_PROJECT_DIR%\%ASSET_SOURCE_REL%"
if not exist "%ASSET_SOURCE%" (
    echo   ERROR: Asset not found: %ASSET_SOURCE_REL%
    exit /b 1
)

if exist "!ASSET_SOURCE!\" (
    if /I "!ASSET_TARGET_REL!"=="." (
        set "ASSET_DEST=%BIN_DIR%"
    ) else (
        set "ASSET_DEST=%BIN_DIR%\!ASSET_TARGET_REL!"
    )
    if not exist "!ASSET_DEST!" mkdir "!ASSET_DEST!"
    robocopy "!ASSET_SOURCE!" "!ASSET_DEST!" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 (
        echo   ERROR: Failed to copy asset directory: !ASSET_SOURCE_REL!
        exit /b 1
    )
    exit /b 0
)

if /I "!ASSET_TARGET_REL!"=="." (
    set "ASSET_DEST=%BIN_DIR%"
) else (
    set "ASSET_DEST=%BIN_DIR%\!ASSET_TARGET_REL!"
)
if not exist "!ASSET_DEST!" mkdir "!ASSET_DEST!"
copy /Y "!ASSET_SOURCE!" "!ASSET_DEST!\" >nul
if errorlevel 1 (
    echo   ERROR: Failed to copy asset file: !ASSET_SOURCE_REL!
    exit /b 1
)
exit /b 0

REM ============================================
REM Ensure a Zanna tool/runtime build exists for the requested demo arch.
REM ============================================
:ensure_zanna_build
echo Zanna tool not found at %ZANNA%
echo Configuring/building target-architecture Zanna runtime...

set "ARCH_ARG="
if "%BUILD_DIR_EXPLICIT%"=="0" (
    if "%ZANNA_CMAKE_GENERATOR%"=="" (
        if /I "%DEMO_ARCH%"=="arm64" set "ARCH_ARG=-A ARM64"
        if /I "%DEMO_ARCH%"=="x64" set "ARCH_ARG=-A x64"
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
    echo ERROR: CMake configuration failed for %DEMO_ARCH% build
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config %ZANNA_BUILD_TYPE% --target zanna -j %JOBS%
if errorlevel 1 (
    echo ERROR: Zanna build failed for %DEMO_ARCH%
    exit /b 1
)

if not exist "%ZANNA%" (
    echo ERROR: zanna still not found at %ZANNA%
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
        if /I "%HOST_DEMO_ARCH%"=="arm64" set "TOOL_ARCH_ARG=-A ARM64"
        if /I "%HOST_DEMO_ARCH%"=="x64" set "TOOL_ARCH_ARG=-A x64"
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
