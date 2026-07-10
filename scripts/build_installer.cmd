@echo off
setlocal DisableDelayedExpansion

set "ROOT_DIR=%~dp0.."
if "%VIPER_BUILD_DIR%"=="" set "VIPER_BUILD_DIR=%ROOT_DIR%\build"
if "%VIPER_BUILD_TYPE%"=="" set "VIPER_BUILD_TYPE=Release"
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
if "%VIPER_SKIP_INSTALL%"=="" set "VIPER_SKIP_INSTALL=1"
if "%USES_EXISTING_INPUT%"=="0" (
    set VIPER_EXTRA_CMAKE_ARGS 2>nul | findstr /C:"-DVIPER_INSTALL_VIPERIDE=" >nul
    if errorlevel 1 (
        set "VIPER_EXTRA_CMAKE_ARGS=%VIPER_EXTRA_CMAKE_ARGS% -DVIPER_INSTALL_VIPERIDE=ON"
    )
    call "%ROOT_DIR%\scripts\build_viper_win.cmd"
    if errorlevel 1 exit /b 1
)

set "VIPER_EXE=%VIPER_BUILD_DIR%\src\tools\viper\viper.exe"
if exist "%VIPER_BUILD_DIR%\src\tools\viper\%VIPER_BUILD_TYPE%\viper.exe" (
    set "VIPER_EXE=%VIPER_BUILD_DIR%\src\tools\viper\%VIPER_BUILD_TYPE%\viper.exe"
)
if not exist "%VIPER_EXE%" (
    echo ERROR: viper executable not found at "%VIPER_EXE%"
    echo Build Viper first or set VIPER_BUILD_DIR to an existing build tree.
    exit /b 1
)

if "%USES_EXISTING_INPUT%"=="0" (
    if "%HAS_EXPLICIT_BUILD_DIR%"=="0" (
        "%VIPER_EXE%" install-package --build-dir "%VIPER_BUILD_DIR%" --config %VIPER_BUILD_TYPE% --skip-build %FORWARD_ARGS%
        exit /b %ERRORLEVEL%
    )
)
"%VIPER_EXE%" install-package %FORWARD_ARGS%
exit /b %ERRORLEVEL%
