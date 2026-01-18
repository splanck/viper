@echo off
setlocal enabledelayedexpansion

REM Build native x86_64 binaries for all demos on Windows
REM Usage: scripts\build_demos.cmd [--clean]

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build"
set "BIN_DIR=%ROOT_DIR%\demos\bin"
set "BASIC_DIR=%ROOT_DIR%\demos\basic"
set "ZIA_DIR=%ROOT_DIR%\demos\zia"
set "TMP_DIR=%TEMP%\viper_demo_build_%RANDOM%"

set "ILC=%BUILD_DIR%\src\tools\ilc\Debug\ilc.exe"
set "ZIA=%BUILD_DIR%\src\tools\zia\Debug\zia.exe"
set "RUNTIME_LIB=%BUILD_DIR%\src\runtime\Debug\viper_runtime.lib"
set "GFX_LIB=%BUILD_DIR%\lib\Debug\vipergfx.lib"
set "GUI_LIB=%BUILD_DIR%\src\lib\gui\Debug\vipergui.lib"

set CLEAN=0
set FAILED=0
set SUCCEEDED=0

REM Parse arguments
:parse_args
if "%~1"=="" goto :done_args
if "%~1"=="--clean" (
    set CLEAN=1
    shift
    goto :parse_args
)
if "%~1"=="-h" goto :usage
if "%~1"=="--help" goto :usage
echo Unknown argument: %~1
goto :usage

:usage
echo Usage: %~nx0 [--clean]
echo   --clean    Remove existing binaries before building
exit /b 1

:done_args

echo Building Viper demos as native x86_64 binaries
echo ==============================================
echo.

REM Check prerequisites
if not exist "%ILC%" (
    echo ERROR: ilc not found at %ILC%
    echo Run 'cmake --build build' first
    exit /b 1
)

if not exist "%RUNTIME_LIB%" (
    echo ERROR: Runtime library not found at %RUNTIME_LIB%
    echo Run 'cmake --build build' first
    exit /b 1
)

if not exist "%ZIA%" (
    echo ERROR: zia not found at %ZIA%
    echo Run 'cmake --build build' first
    exit /b 1
)

REM Create directories
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
if not exist "%TMP_DIR%" mkdir "%TMP_DIR%"

if %CLEAN%==1 (
    echo Cleaning existing binaries...
    del /q "%BIN_DIR%\*" 2>nul
)

echo === BASIC Demos ===
echo.

REM Build BASIC demos
call :build_basic_demo chess "%BASIC_DIR%\chess\chess.bas"
call :build_basic_demo vtris "%BASIC_DIR%\vtris\vtris.bas"
call :build_basic_demo frogger "%BASIC_DIR%\frogger\frogger.bas"
call :build_basic_demo centipede "%BASIC_DIR%\centipede\centipede.bas"
call :build_basic_demo pacman "%BASIC_DIR%\pacman\pacman.bas"

echo === Zia Demos ===
echo.

REM Build Zia demos
call :build_zia_demo paint "%ZIA_DIR%\paint\main.zia"

echo ==============================================
if %FAILED%==0 (
    echo All %SUCCEEDED% demos built successfully!
    echo.
    echo Binaries are in: %BIN_DIR%
    dir "%BIN_DIR%"
) else (
    echo %FAILED% demo(s^) failed, %SUCCEEDED% succeeded
)

REM Cleanup
rmdir /s /q "%TMP_DIR%" 2>nul

exit /b %FAILED%

REM ============================================
REM Build a BASIC demo
REM ============================================
:build_basic_demo
set "NAME=%~1"
set "SOURCE=%~2"

echo Building %NAME%...

if not exist "%SOURCE%" (
    echo   ERROR: Source file not found: %SOURCE%
    set /a FAILED+=1
    echo.
    goto :eof
)

set "IL_FILE=%TMP_DIR%\%NAME%.il"
set "ASM_FILE=%TMP_DIR%\%NAME%.s"
set "OBJ_FILE=%TMP_DIR%\%NAME%.obj"
set "EXE_FILE=%BIN_DIR%\%NAME%.exe"

echo   Compiling BASIC to IL...
"%ILC%" front basic -emit-il "%SOURCE%" > "%IL_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED: BASIC compilation
    set /a FAILED+=1
    echo.
    goto :eof
)
echo   OK

echo   Generating x86_64 assembly...
"%ILC%" codegen x64 "%IL_FILE%" -S "%ASM_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED: x86_64 codegen
    echo   (This is expected - x86_64 backend is incomplete on Windows^)
    set /a FAILED+=1
    echo.
    goto :eof
)
echo   OK

echo   Assembling and linking...
REM Use clang to assemble and link if available
where clang >nul 2>nul
if errorlevel 1 (
    echo   FAILED: clang not found in PATH
    echo   Install LLVM/Clang to assemble and link
    set /a FAILED+=1
    echo.
    goto :eof
)

clang -c "%ASM_FILE%" -o "%OBJ_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED: Assembly
    set /a FAILED+=1
    echo.
    goto :eof
)

REM Use MSVC-style linking to avoid CRT conflicts
clang++ -fuse-ld=lld-link "%OBJ_FILE%" "%RUNTIME_LIB%" -Xlinker /DEFAULTLIB:msvcrtd -luser32 -lkernel32 -lws2_32 -ladvapi32 -lxinput -o "%EXE_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED: Linking
    set /a FAILED+=1
    echo.
    goto :eof
)
echo   OK

echo   Built: %EXE_FILE%
set /a SUCCEEDED+=1
echo.
goto :eof

REM ============================================
REM Build a Zia demo
REM ============================================
:build_zia_demo
set "NAME=%~1"
set "SOURCE=%~2"

echo Building %NAME%...

if not exist "%SOURCE%" (
    echo   ERROR: Source file not found: %SOURCE%
    set /a FAILED+=1
    echo.
    goto :eof
)

set "IL_FILE=%TMP_DIR%\%NAME%.il"
set "ASM_FILE=%TMP_DIR%\%NAME%.s"
set "OBJ_FILE=%TMP_DIR%\%NAME%.obj"
set "EXE_FILE=%BIN_DIR%\%NAME%.exe"

echo   Compiling Zia to IL...
"%ZIA%" "%SOURCE%" --emit-il > "%IL_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED: Zia compilation
    "%ZIA%" "%SOURCE%" --emit-il 2>&1
    set /a FAILED+=1
    echo.
    goto :eof
)
echo   OK

echo   Generating x86_64 assembly...
"%ILC%" codegen x64 "%IL_FILE%" -S "%ASM_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED: x86_64 codegen
    echo   (This is expected - x86_64 backend is incomplete on Windows^)
    set /a FAILED+=1
    echo.
    goto :eof
)
echo   OK

echo   Assembling and linking...
where clang >nul 2>nul
if errorlevel 1 (
    echo   FAILED: clang not found in PATH
    echo   Install LLVM/Clang to assemble and link
    set /a FAILED+=1
    echo.
    goto :eof
)

clang -c "%ASM_FILE%" -o "%OBJ_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED: Assembly
    set /a FAILED+=1
    echo.
    goto :eof
)

REM Use MSVC-style linking to avoid CRT conflicts
clang++ -fuse-ld=lld-link "%OBJ_FILE%" "%RUNTIME_LIB%" "%GFX_LIB%" "%GUI_LIB%" -Xlinker /DEFAULTLIB:msvcrtd -luser32 -lgdi32 -lkernel32 -lws2_32 -ladvapi32 -lxinput -o "%EXE_FILE%" 2>nul
if errorlevel 1 (
    echo   FAILED: Linking
    set /a FAILED+=1
    echo.
    goto :eof
)
echo   OK

echo   Built: %EXE_FILE%
set /a SUCCEEDED+=1
echo.
goto :eof
