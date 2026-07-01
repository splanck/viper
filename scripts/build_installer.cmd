@echo off
setlocal enabledelayedexpansion

set "ROOT_DIR=%~dp0.."
if "%VIPER_BUILD_DIR%"=="" set "VIPER_BUILD_DIR=%ROOT_DIR%\build"
if "%VIPER_BUILD_TYPE%"=="" set "VIPER_BUILD_TYPE=Release"
if "%NUMBER_OF_PROCESSORS%"=="" (
    set "JOBS=8"
) else (
    set "JOBS=%NUMBER_OF_PROCESSORS%"
)

if not exist "%VIPER_BUILD_DIR%\CMakeCache.txt" (
    echo Configuring Viper build tree...
    if "%VIPER_CMAKE_GENERATOR%"=="" (
        cmake -S "%ROOT_DIR%" -B "%VIPER_BUILD_DIR%" -DCMAKE_BUILD_TYPE=%VIPER_BUILD_TYPE% -DVIPER_INSTALL_VIPERIDE=ON %VIPER_EXTRA_CMAKE_ARGS%
        if errorlevel 1 exit /b 1
    ) else (
        cmake -S "%ROOT_DIR%" -B "%VIPER_BUILD_DIR%" -G "%VIPER_CMAKE_GENERATOR%" -DCMAKE_BUILD_TYPE=%VIPER_BUILD_TYPE% -DVIPER_INSTALL_VIPERIDE=ON %VIPER_EXTRA_CMAKE_ARGS%
        if errorlevel 1 exit /b 1
    )
)

echo Building Viper toolchain payload...
cmake --build "%VIPER_BUILD_DIR%" --config %VIPER_BUILD_TYPE% -j %JOBS%
if errorlevel 1 exit /b 1

set "VIPER_EXE=%VIPER_BUILD_DIR%\src\tools\viper\viper.exe"
if exist "%VIPER_BUILD_DIR%\src\tools\viper\%VIPER_BUILD_TYPE%\viper.exe" (
    set "VIPER_EXE=%VIPER_BUILD_DIR%\src\tools\viper\%VIPER_BUILD_TYPE%\viper.exe"
)

set "FORWARD_ARGS=%*"
set "HAS_STAGE_MODE=0"
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--build-dir" set "HAS_STAGE_MODE=1"
if /I "%~1"=="--stage-dir" set "HAS_STAGE_MODE=1"
if /I "%~1"=="--verify-only" set "HAS_STAGE_MODE=1"
shift
goto parse_args

:args_done
if "%HAS_STAGE_MODE%"=="0" (
    "%VIPER_EXE%" install-package --build-dir "%VIPER_BUILD_DIR%" --config %VIPER_BUILD_TYPE% --skip-build %FORWARD_ARGS%
) else (
    "%VIPER_EXE%" install-package %FORWARD_ARGS%
)
exit /b %ERRORLEVEL%
