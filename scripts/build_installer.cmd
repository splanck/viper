@echo off
setlocal DisableDelayedExpansion

set "ROOT_DIR=%~dp0.."
if "%ZANNA_BUILD_DIR%"=="" set "ZANNA_BUILD_DIR=%ROOT_DIR%\build"
if "%ZANNA_BUILD_TYPE%"=="" set "ZANNA_BUILD_TYPE=Release"
if "%NUMBER_OF_PROCESSORS%"=="" (
    set "JOBS=8"
) else (
    set "JOBS=%NUMBER_OF_PROCESSORS%"
)

set "FORWARD_ARGS=%*"
set "USES_EXISTING_INPUT=0"
set "HAS_EXPLICIT_BUILD_DIR=0"
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--build-dir" set "HAS_EXPLICIT_BUILD_DIR=1"
if /I "%~1"=="--stage-dir" set "USES_EXISTING_INPUT=1"
if /I "%~1"=="--verify-only" set "USES_EXISTING_INPUT=1"
shift
goto parse_args

:args_done
if "%ZANNA_SKIP_INSTALL%"=="" set "ZANNA_SKIP_INSTALL=1"
if "%USES_EXISTING_INPUT%"=="0" (
    set ZANNA_EXTRA_CMAKE_ARGS 2>nul | findstr /C:"-DZANNA_INSTALL_ZANNAIDE=" >nul
    if errorlevel 1 (
        set "ZANNA_EXTRA_CMAKE_ARGS=%ZANNA_EXTRA_CMAKE_ARGS% -DZANNA_INSTALL_ZANNAIDE=ON"
    )
    call "%ROOT_DIR%\scripts\build_zanna_win.cmd"
    if errorlevel 1 exit /b 1
)

set "ZANNA_EXE=%ZANNA_BUILD_DIR%\src\tools\zanna\zanna.exe"
if exist "%ZANNA_BUILD_DIR%\src\tools\zanna\%ZANNA_BUILD_TYPE%\zanna.exe" (
    set "ZANNA_EXE=%ZANNA_BUILD_DIR%\src\tools\zanna\%ZANNA_BUILD_TYPE%\zanna.exe"
)
if not exist "%ZANNA_EXE%" (
    echo ERROR: zanna executable not found at "%ZANNA_EXE%"
    echo Build Zanna first or set ZANNA_BUILD_DIR to an existing build tree.
    exit /b 1
)

if "%USES_EXISTING_INPUT%"=="0" (
    if "%HAS_EXPLICIT_BUILD_DIR%"=="0" (
        "%ZANNA_EXE%" install-package --build-dir "%ZANNA_BUILD_DIR%" --config %ZANNA_BUILD_TYPE% --skip-build %FORWARD_ARGS%
        exit /b %ERRORLEVEL%
    )
)
"%ZANNA_EXE%" install-package %FORWARD_ARGS%
exit /b %ERRORLEVEL%
