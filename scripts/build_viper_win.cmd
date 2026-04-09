@echo off
setlocal enabledelayedexpansion
set TESTS_FAILED=0

REM build_viper.cmd - Windows Viper build + test + install script.
REM Matches robustness and features of the Unix build scripts.

echo ==========================================
echo Building Viper on Windows
echo ==========================================
echo.

REM --- Parallel job detection -------------------------------------------------
set JOBS=%NUMBER_OF_PROCESSORS%
if "%JOBS%"=="" set JOBS=8
echo Using %JOBS% parallel jobs

if "%VIPER_BUILD_DIR%"=="" set "VIPER_BUILD_DIR=build"
if "%VIPER_BUILD_TYPE%"=="" set "VIPER_BUILD_TYPE=Debug"
if "%VIPER_SKIP_INSTALL%"=="" set "VIPER_SKIP_INSTALL=0"
if "%VIPER_SKIP_LINT%"=="" set "VIPER_SKIP_LINT=0"
if "%VIPER_SKIP_AUDIT%"=="" set "VIPER_SKIP_AUDIT=0"
if "%VIPER_SKIP_SMOKE%"=="" set "VIPER_SKIP_SMOKE=0"

set "CONFIG_ARGS="
if not "%VIPER_CMAKE_GENERATOR%"=="" (
    set "CONFIG_ARGS=%CONFIG_ARGS% -G "%VIPER_CMAKE_GENERATOR%""
)
if not "%VIPER_EXTRA_CMAKE_ARGS%"=="" (
    set "CONFIG_ARGS=%CONFIG_ARGS% %VIPER_EXTRA_CMAKE_ARGS%"
)

REM --- Compiler detection -----------------------------------------------------
set COMPILER_FLAGS=
where clang-cl >nul 2>&1
if not errorlevel 1 (
    echo Using clang-cl
    set COMPILER_FLAGS=-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
) else (
    echo Using default compiler MSVC
)
echo.

REM --- Clean previous build ---------------------------------------------------
cmake --build "%VIPER_BUILD_DIR%" --target clean-all 2>nul
if errorlevel 1 (
    REM clean-all target may not exist on first build; ignore
)

REM --- Configure --------------------------------------------------------------
echo Configuring with CMake...
cmake -S . -B "%VIPER_BUILD_DIR%" %COMPILER_FLAGS% -DCMAKE_BUILD_TYPE=%VIPER_BUILD_TYPE% %CONFIG_ARGS%
if errorlevel 1 (
    echo ERROR: CMake configuration failed
    exit /b 1
)
echo.

REM --- Build ------------------------------------------------------------------
echo Building with %JOBS% jobs...
cmake --build "%VIPER_BUILD_DIR%" --config %VIPER_BUILD_TYPE% -j %JOBS%
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)
echo.

REM --- Flush filesystem I/O before tests -------------------------------------
REM Windows does not have sync(1); a brief pause lets pending writes complete.
timeout /t 1 /nobreak >nul 2>&1

REM --- Clear test cache -------------------------------------------------------
if exist "%VIPER_BUILD_DIR%\Testing" rmdir /s /q "%VIPER_BUILD_DIR%\Testing"

REM --- Run tests --------------------------------------------------------------
echo Running tests...
ctest --test-dir "%VIPER_BUILD_DIR%" -C %VIPER_BUILD_TYPE% --output-on-failure -j %JOBS%
if errorlevel 1 (
    set TESTS_FAILED=1
    echo.
    echo WARNING: Some tests failed
) else (
    echo.
    echo All tests passed.
)

if "%VIPER_SKIP_LINT%"=="0" (
    where bash >nul 2>&1
    if not errorlevel 1 (
        bash scripts/lint_platform_policy.sh
        if errorlevel 1 set TESTS_FAILED=1
    )
)

if "%VIPER_SKIP_AUDIT%"=="0" (
    where bash >nul 2>&1
    if not errorlevel 1 (
        bash scripts/audit_runtime_surface.sh --build-dir="%VIPER_BUILD_DIR%"
        if errorlevel 1 set TESTS_FAILED=1
    )
)

if "%VIPER_SKIP_SMOKE%"=="0" (
    where bash >nul 2>&1
    if not errorlevel 1 (
        bash scripts/run_cross_platform_smoke.sh --build-dir "%VIPER_BUILD_DIR%"
        if errorlevel 1 set TESTS_FAILED=1
    )
)

REM --- Install ----------------------------------------------------------------
echo.
if "%VIPER_SKIP_INSTALL%"=="1" (
    echo Skipping install ^(VIPER_SKIP_INSTALL=1^)
    goto :build_done
)

echo Installing Viper...
if defined LOCALAPPDATA (
    set INSTALL_PREFIX=%LOCALAPPDATA%\viper
) else (
    set INSTALL_PREFIX=%USERPROFILE%\viper
)
if not "%VIPER_INSTALL_PREFIX%"=="" set INSTALL_PREFIX=%VIPER_INSTALL_PREFIX%
cmake --install "%VIPER_BUILD_DIR%" --prefix "%INSTALL_PREFIX%" --config %VIPER_BUILD_TYPE%
if errorlevel 1 (
    echo WARNING: Install failed
) else (
    echo Installed to %INSTALL_PREFIX%
)

:build_done
echo.
echo ==========================================
echo Build complete
echo ==========================================

if %TESTS_FAILED% neq 0 exit /b 1
