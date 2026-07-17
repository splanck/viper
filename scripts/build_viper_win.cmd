@echo off
REM ===----------------------------------------------------------------------===
REM
REM Part of the Viper project, under the GNU GPL v3.
REM See LICENSE for license information.
REM
REM ===----------------------------------------------------------------------===
REM
REM File: scripts/build_viper_win.cmd
REM Purpose: Configure, build, test, audit, and install Viper on Windows.
REM
REM Key invariants:
REM   - User-selected generators and compilers are forwarded without literal
REM     shell-escaping characters.
REM   - Only a validated POSIX shell is used by CTest and script-based checks.
REM   - A failed validation stage causes the script to return a nonzero status.
REM
REM Ownership/Lifetime: Build outputs remain in VIPER_BUILD_DIR.
REM
REM Links: AGENTS.md, docs/internals/testing.md
REM
REM ===----------------------------------------------------------------------===

setlocal DisableDelayedExpansion

REM Windows environment lookups are case-insensitive, but an inherited process
REM block can still contain both Path and PATH. MSBuild's .NET ToolTask rejects
REM that duplicate while launching CL.exe, so recreate the entry once using a
REM canonical spelling before invoking any build tools.
set "VIPER_NORMALIZED_PATH=%PATH%"
set "Path="
set "PATH="
set "PATH=%VIPER_NORMALIZED_PATH%"
set "VIPER_NORMALIZED_PATH="

setlocal EnableDelayedExpansion
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
if not "%VIPER_CTEST_JOBS%"=="" (
    set "CTEST_JOBS=%VIPER_CTEST_JOBS%"
) else (
    set "CTEST_JOBS=%JOBS%"
)
echo Using %JOBS% build jobs
echo Using %CTEST_JOBS% CTest jobs

REM Do not leave Visual Studio worker nodes resident after the script exits.
if "%MSBUILDDISABLENODEREUSE%"=="" set "MSBUILDDISABLENODEREUSE=1"

if "%VIPER_BUILD_DIR%"=="" set "VIPER_BUILD_DIR=build"
if "%VIPER_BUILD_TYPE%"=="" set "VIPER_BUILD_TYPE=Debug"
if "%VIPER_SKIP_INSTALL%"=="" set "VIPER_SKIP_INSTALL=0"
if "%VIPER_SKIP_TESTS%"=="" set "VIPER_SKIP_TESTS=0"
if "%VIPER_SKIP_LINT%"=="" set "VIPER_SKIP_LINT=0"
if "%VIPER_SKIP_AUDIT%"=="" set "VIPER_SKIP_AUDIT=0"
if "%VIPER_LINT_CHANGED_ONLY%"=="" set "VIPER_LINT_CHANGED_ONLY=1"
if "%VIPER_SKIP_SMOKE%"=="" set "VIPER_SKIP_SMOKE=0"
if "%VIPER_SKIP_CLEAN%"=="" set "VIPER_SKIP_CLEAN=0"
if "%VIPER_RUN_SLOW_TESTS%"=="" set "VIPER_RUN_SLOW_TESTS=0"
if "%VIPER_FAST_DEBUG%"=="" set "VIPER_FAST_DEBUG=1"
set "BASH_BUILD_DIR=%VIPER_BUILD_DIR:\=/%"
echo Build type: %VIPER_BUILD_TYPE%
echo Fast Debug: %VIPER_FAST_DEBUG%

REM --- POSIX shell selection -------------------------------------------------
REM Windows ships a bash.exe WSL launcher even when no WSL distribution exists.
REM Prefer Git Bash, validate every candidate, and forward the exact executable
REM to CMake so shell-backed CTests use the same working shell as later stages.
set "BASH_EXE="
call :find_usable_bash
if defined BASH_EXE (
    echo POSIX shell: %BASH_EXE%
    REM The sanitizer and coverage self-tests require Clang. The official
    REM Windows LLVM installer does not always add its default bin directory.
    where clang >nul 2>&1
    if errorlevel 1 if defined ProgramFiles if exist "%ProgramFiles%\LLVM\bin\clang.exe" (
        set "PATH=%PATH%;%ProgramFiles%\LLVM\bin"
    )
) else (
    echo POSIX shell: unavailable
)

set "CONFIG_ARGS=-DVIPER_FAST_DEBUG=%VIPER_FAST_DEBUG%"
if defined BASH_EXE (
    set "CONFIG_ARGS=%CONFIG_ARGS% -DVIPER_BASH_EXECUTABLE:FILEPATH="%BASH_EXE%""
)
if not "%VIPER_CMAKE_GENERATOR%"=="" (
    set "CONFIG_ARGS=%CONFIG_ARGS% -G "%VIPER_CMAKE_GENERATOR%""
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
if "%VIPER_SKIP_CLEAN%"=="1" (
    echo Skipping clean ^(VIPER_SKIP_CLEAN=1^); incremental rebuild
) else if "%CACHE_RESET%"=="0" (
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
set "CTEST_LABEL_ARGS="
if not "%VIPER_TEST_LABEL%"=="" (
    echo Running only tests labeled "%VIPER_TEST_LABEL%" ^(VIPER_TEST_LABEL^)
    set "CTEST_LABEL_ARGS=-L %VIPER_TEST_LABEL%"
)
if not "%VIPER_RUN_SLOW_TESTS%"=="1" (
    echo Skipping tests labeled slow ^(set VIPER_RUN_SLOW_TESTS=1 to include them^)
    set "CTEST_LABEL_ARGS=%CTEST_LABEL_ARGS% -LE slow"
)
set "CTEST_PRETTY_SCRIPT=%~dp0run_ctest_pretty.ps1"
set "CTEST_USE_PRETTY=0"
where powershell >nul 2>&1
if not errorlevel 1 if exist "%CTEST_PRETTY_SCRIPT%" set "CTEST_USE_PRETTY=1"
if "%CTEST_USE_PRETTY%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%CTEST_PRETTY_SCRIPT%" --test-dir "%VIPER_BUILD_DIR%" -C %VIPER_BUILD_TYPE% --output-on-failure -j %CTEST_JOBS% %CTEST_LABEL_ARGS%
) else (
    ctest --test-dir "%VIPER_BUILD_DIR%" -C %VIPER_BUILD_TYPE% --output-on-failure -j %CTEST_JOBS% %CTEST_LABEL_ARGS%
)
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
    if defined BASH_EXE (
        echo Running platform policy lint...
        if "%VIPER_LINT_CHANGED_ONLY%"=="1" (
            "%BASH_EXE%" --login scripts/lint_platform_policy.sh --changed-only
        ) else (
            "%BASH_EXE%" --login scripts/lint_platform_policy.sh
        )
        if errorlevel 1 set TESTS_FAILED=1
    ) else (
        echo Skipping platform policy lint ^(no usable POSIX shell found^)
    )
)

if "%VIPER_SKIP_AUDIT%"=="0" (
    if defined BASH_EXE (
        echo Running runtime surface audit...
        "%BASH_EXE%" --login scripts/audit_runtime_surface.sh --build-dir="%BASH_BUILD_DIR%" --config="%VIPER_BUILD_TYPE%"
        if errorlevel 1 set TESTS_FAILED=1
    ) else (
        echo Skipping runtime surface audit ^(no usable POSIX shell found^)
    )
)

if "%VIPER_SKIP_SMOKE%"=="0" (
    if defined BASH_EXE (
        echo Running cross-platform smoke tests...
        "%BASH_EXE%" --login scripts/run_cross_platform_smoke.sh --build-dir "%BASH_BUILD_DIR%" --config "%VIPER_BUILD_TYPE%"
        if errorlevel 1 set TESTS_FAILED=1
    ) else (
        echo Skipping cross-platform smoke tests ^(no usable POSIX shell found^)
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

:find_usable_bash
set "BASH_EXE="
if defined VIPER_BASH_EXECUTABLE (
    call :try_bash "%VIPER_BASH_EXECUTABLE%"
    if defined BASH_EXE exit /b 0
    echo WARNING: VIPER_BASH_EXECUTABLE is not usable: %VIPER_BASH_EXECUTABLE%
)
if defined ProgramFiles (
    call :try_bash "%ProgramFiles%\Git\bin\bash.exe"
    if defined BASH_EXE exit /b 0
    call :try_bash "%ProgramFiles%\Git\usr\bin\bash.exe"
    if defined BASH_EXE exit /b 0
)
if defined LOCALAPPDATA (
    call :try_bash "%LOCALAPPDATA%\Programs\Git\bin\bash.exe"
    if defined BASH_EXE exit /b 0
)
for /f "delims=" %%B in ('where bash 2^>nul') do (
    if not defined BASH_EXE call :try_bash "%%B"
)
exit /b 0

:try_bash
if not exist "%~1" exit /b 1
"%~1" -lc "exit 0" >nul 2>&1
if errorlevel 1 exit /b 1
set "BASH_EXE=%~f1"
exit /b 0
