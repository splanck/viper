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
cmake --build build --target clean-all 2>nul
if errorlevel 1 (
    REM clean-all target may not exist on first build; ignore
)

REM --- Configure --------------------------------------------------------------
echo Configuring with CMake...
cmake -S . -B build %COMPILER_FLAGS%
if errorlevel 1 (
    echo ERROR: CMake configuration failed
    exit /b 1
)
echo.

REM --- Build ------------------------------------------------------------------
echo Building with %JOBS% jobs...
cmake --build build --config Debug -j %JOBS%
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)
echo.

REM --- Flush filesystem I/O before tests -------------------------------------
REM Windows does not have sync(1); a brief pause lets pending writes complete.
timeout /t 1 /nobreak >nul 2>&1

REM --- Clear test cache -------------------------------------------------------
if exist build\Testing rmdir /s /q build\Testing

REM --- Run tests --------------------------------------------------------------
echo Running tests...
ctest --test-dir build -C Debug --output-on-failure -j %JOBS%
if errorlevel 1 (
    set TESTS_FAILED=1
    echo.
    echo WARNING: Some tests failed
) else (
    echo.
    echo All tests passed.
)

REM --- Install ----------------------------------------------------------------
echo.
echo Installing Viper...
if defined LOCALAPPDATA (
    set INSTALL_PREFIX=%LOCALAPPDATA%\viper
) else (
    set INSTALL_PREFIX=%USERPROFILE%\viper
)
cmake --install build --prefix "%INSTALL_PREFIX%" --config Debug
if errorlevel 1 (
    echo WARNING: Install failed
) else (
    echo Installed to %INSTALL_PREFIX%
)

echo.
echo ==========================================
echo Build complete
echo ==========================================

if %TESTS_FAILED% neq 0 exit /b 1
