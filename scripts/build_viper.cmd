@echo off
setlocal enabledelayedexpansion

REM Build Viper on Windows
REM Usage: scripts\build_viper.cmd

echo ==========================================
echo Building Viper on Windows
echo ==========================================
echo.

REM Clean previous build
if exist build (
    echo Removing previous build directory...
    rmdir /s /q build
    echo.
)

REM Configure
echo Configuring with CMake...
cmake -S . -B build
if errorlevel 1 (
    echo ERROR: CMake configuration failed
    exit /b 1
)
echo.

REM Build
echo Building...
cmake --build build --config Debug -j
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)
echo.

REM Clear test cache
if exist build\Testing rmdir /s /q build\Testing

REM Run tests
echo Running tests...
ctest --test-dir build -C Debug --output-on-failure
if errorlevel 1 (
    echo.
    echo WARNING: Some tests failed
) else (
    echo.
    echo All tests passed!
)

echo.
echo ==========================================
echo Build complete!
echo ==========================================
