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
REM MSVC can exhaust compiler heap when a large clean build fans out across every
REM logical CPU. Keep the automatic default conservative; VIPER_JOBS remains an
REM explicit opt-in for larger builders.
if not "%VIPER_JOBS%"=="" (
    set "JOBS=%VIPER_JOBS%"
) else (
    set "JOBS=%NUMBER_OF_PROCESSORS%"
    if "!JOBS!"=="" set "JOBS=8"
    if !JOBS! GTR 8 set "JOBS=8"
)
echo Using %JOBS% parallel jobs

if "%VIPER_BUILD_DIR%"=="" set "VIPER_BUILD_DIR=build"
if "%VIPER_BUILD_TYPE%"=="" set "VIPER_BUILD_TYPE=Debug"
if "%VIPER_SKIP_INSTALL%"=="" set "VIPER_SKIP_INSTALL=0"
if "%VIPER_SKIP_TESTS%"=="" set "VIPER_SKIP_TESTS=0"
if "%VIPER_SKIP_LINT%"=="" set "VIPER_SKIP_LINT=0"
if "%VIPER_SKIP_AUDIT%"=="" set "VIPER_SKIP_AUDIT=0"
if "%VIPER_LINT_CHANGED_ONLY%"=="" set "VIPER_LINT_CHANGED_ONLY=1"
if "%VIPER_SKIP_SMOKE%"=="" set "VIPER_SKIP_SMOKE=0"

set "CONFIG_ARGS="
if not "%VIPER_CMAKE_GENERATOR%"=="" (
    set "CONFIG_ARGS=%CONFIG_ARGS% -G \"%VIPER_CMAKE_GENERATOR%\""
)
if not "%VIPER_EXTRA_CMAKE_ARGS%"=="" (
    set "CONFIG_ARGS=%CONFIG_ARGS% %VIPER_EXTRA_CMAKE_ARGS%"
)
if "%VIPER_WARN_AS_ERROR%"=="" (
    if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
        set "CONFIG_ARGS=%CONFIG_ARGS% -DIL_WARN_AS_ERROR=OFF"
    )
) else (
    set "CONFIG_ARGS=%CONFIG_ARGS% -DIL_WARN_AS_ERROR=%VIPER_WARN_AS_ERROR%"
)

REM --- Compiler selection -----------------------------------------------------
REM Default to MSVC on Windows. Opt into clang-cl with VIPER_WINDOWS_COMPILER=clang-cl.
set COMPILER_FLAGS=
set "REQUESTED_WINDOWS_COMPILER=msvc"
if /I "%VIPER_WINDOWS_COMPILER%"=="clang-cl" (
    where clang-cl >nul 2>&1
    if errorlevel 1 (
        echo ERROR: VIPER_WINDOWS_COMPILER=clang-cl but clang-cl was not found in PATH
        exit /b 1
    )
    echo Using clang-cl ^(VIPER_WINDOWS_COMPILER=clang-cl^)
    set "REQUESTED_WINDOWS_COMPILER=clang-cl"
    set COMPILER_FLAGS=-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
) else (
    echo Using default compiler MSVC
)
echo.

REM --- Cached compiler validation --------------------------------------------
REM CMake stores the selected compiler as an absolute path. Visual Studio updates
REM can remove that path, so clear only the configure cache when it points at a
REM missing or no-longer-requested compiler.
set "CACHE_RESET=0"
call :reset_stale_compiler_cache
if errorlevel 1 exit /b 1

REM --- Clean previous build ---------------------------------------------------
if "%CACHE_RESET%"=="0" (
    cmake --build "%VIPER_BUILD_DIR%" --target clean-all 2>nul
    if errorlevel 1 (
        REM clean-all target may not exist on first build; ignore
    )
) else (
    echo Skipping pre-configure clean because cached compiler state was reset.
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
if "%VIPER_SKIP_TESTS%"=="1" (
    echo Skipping tests ^(VIPER_SKIP_TESTS=1^)
    goto :after_tests
)

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

:after_tests

if "%VIPER_SKIP_LINT%"=="0" (
    where bash >nul 2>&1
    if not errorlevel 1 (
        echo Running platform policy lint...
        if "%VIPER_LINT_CHANGED_ONLY%"=="1" (
            bash scripts/lint_platform_policy.sh --changed-only
        ) else (
            bash scripts/lint_platform_policy.sh
        )
        if errorlevel 1 set TESTS_FAILED=1
    )
)

if "%VIPER_SKIP_AUDIT%"=="0" (
    where bash >nul 2>&1
    if not errorlevel 1 (
        echo Running runtime surface audit...
        bash scripts/audit_runtime_surface.sh --build-dir="%VIPER_BUILD_DIR%"
        if errorlevel 1 set TESTS_FAILED=1
    )
)

if "%VIPER_SKIP_SMOKE%"=="0" (
    where bash >nul 2>&1
    if not errorlevel 1 (
        echo Running cross-platform smoke tests...
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
exit /b 0

:reset_stale_compiler_cache
set "CACHE_FILE=%VIPER_BUILD_DIR%\CMakeCache.txt"
set "STALE_CACHE=0"
if not exist "%CACHE_FILE%" exit /b 0

call :check_cached_compiler CMAKE_C_COMPILER
if errorlevel 2 exit /b 1
if errorlevel 1 set "STALE_CACHE=1"

call :check_cached_compiler CMAKE_CXX_COMPILER
if errorlevel 2 exit /b 1
if errorlevel 1 set "STALE_CACHE=1"

if "%STALE_CACHE%"=="0" exit /b 0

echo Detected stale CMake compiler cache in "%VIPER_BUILD_DIR%".
echo Resetting cached configure state; build outputs will be regenerated as needed.
del /q "%CACHE_FILE%" >nul 2>&1
if exist "%CACHE_FILE%" (
    echo ERROR: Failed to remove "%CACHE_FILE%"
    exit /b 1
)
if exist "%VIPER_BUILD_DIR%\CMakeFiles" (
    rmdir /s /q "%VIPER_BUILD_DIR%\CMakeFiles"
    if exist "%VIPER_BUILD_DIR%\CMakeFiles" (
        echo ERROR: Failed to remove "%VIPER_BUILD_DIR%\CMakeFiles"
        exit /b 1
    )
)
set "CACHE_RESET=1"
exit /b 0

:check_cached_compiler
set "CACHE_KEY=%~1"
set "CACHE_VALUE="
for /f "usebackq tokens=1,* delims==" %%A in ("%CACHE_FILE%") do (
    if /I "%%A"=="%CACHE_KEY%:FILEPATH" set "CACHE_VALUE=%%B"
    if /I "%%A"=="%CACHE_KEY%:STRING" set "CACHE_VALUE=%%B"
)
if "%CACHE_VALUE%"=="" exit /b 0

call :cached_compiler_matches_request "%CACHE_VALUE%"
if errorlevel 1 (
    echo Cached %CACHE_KEY% does not match requested compiler "%REQUESTED_WINDOWS_COMPILER%": %CACHE_VALUE%
    exit /b 1
)

call :tool_exists "%CACHE_VALUE%"
if errorlevel 1 (
    echo Cached %CACHE_KEY% no longer exists: %CACHE_VALUE%
    exit /b 1
)
exit /b 0

:cached_compiler_matches_request
set "CACHE_TOOL=%~nx1"
if /I "%REQUESTED_WINDOWS_COMPILER%"=="clang-cl" (
    if /I "%CACHE_TOOL%"=="clang-cl" exit /b 0
    if /I "%CACHE_TOOL%"=="clang-cl.exe" exit /b 0
    exit /b 1
)
if /I "%CACHE_TOOL%"=="clang-cl" exit /b 1
if /I "%CACHE_TOOL%"=="clang-cl.exe" exit /b 1
exit /b 0

:tool_exists
set "TOOL_VALUE=%~1"
if "%TOOL_VALUE%"=="" exit /b 0
if exist "%TOOL_VALUE%" exit /b 0
echo "%TOOL_VALUE%" | findstr /R "[\\/]" >nul 2>&1
if not errorlevel 1 exit /b 1
where "%TOOL_VALUE%" >nul 2>&1
if errorlevel 1 exit /b 1
exit /b 0
