@echo off
setlocal enabledelayedexpansion

set "ROOT_DIR=%~dp0.."
if "%VIPER_BUILD_DIR%"=="" set "VIPER_BUILD_DIR=%ROOT_DIR%\build"
if "%VIPER_BUILD_TYPE%"=="" set "VIPER_BUILD_TYPE=Debug"
if "%NUMBER_OF_PROCESSORS%"=="" (
    set "JOBS=8"
) else (
    set "JOBS=%NUMBER_OF_PROCESSORS%"
)

if not exist "%VIPER_BUILD_DIR%\CMakeCache.txt" (
    echo Configuring Viper build tree...
    set "CONFIG_ARGS="
    if not "%VIPER_CMAKE_GENERATOR%"=="" (
        set "CONFIG_ARGS=%CONFIG_ARGS% -G "%VIPER_CMAKE_GENERATOR%""
    )
    if not "%VIPER_EXTRA_CMAKE_ARGS%"=="" (
        set "CONFIG_ARGS=%CONFIG_ARGS% %VIPER_EXTRA_CMAKE_ARGS%"
    )
    cmake -S "%ROOT_DIR%" -B "%VIPER_BUILD_DIR%" -DCMAKE_BUILD_TYPE=%VIPER_BUILD_TYPE% %CONFIG_ARGS%
    if errorlevel 1 exit /b 1
)

echo Building viper...
cmake --build "%VIPER_BUILD_DIR%" --config %VIPER_BUILD_TYPE% -j %JOBS% --target viper
if errorlevel 1 exit /b 1

set "FORWARD_ARGS="
set "HAS_STAGE_MODE=0"
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--build-dir" set "HAS_STAGE_MODE=1"
if /I "%~1"=="--stage-dir" set "HAS_STAGE_MODE=1"
if /I "%~1"=="--verify-only" set "HAS_STAGE_MODE=1"
set "FORWARD_ARGS=%FORWARD_ARGS% %1"
shift
goto parse_args

:args_done
if "%HAS_STAGE_MODE%"=="0" (
    "%VIPER_BUILD_DIR%\src\tools\viper\viper.exe" install-package --build-dir "%VIPER_BUILD_DIR%" %FORWARD_ARGS%
) else (
    "%VIPER_BUILD_DIR%\src\tools\viper\viper.exe" install-package %FORWARD_ARGS%
)
exit /b %ERRORLEVEL%
