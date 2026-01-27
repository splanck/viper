@echo off
setlocal EnableDelayedExpansion

REM ViperDOS Complete Build and Run Script for Windows
REM Usage: build_viperdos.cmd [options]
REM   --serial    Run in serial-only mode (no graphics)
REM   --direct    Boot kernel directly (bypass VBoot bootloader)
REM   --debug     Enable GDB debugging (wait on port 1234)
REM   --no-net    Disable networking
REM   --test      Run tests before launching QEMU
REM   --no-run    Do not launch QEMU (build/test only)
REM   --help      Show this help

REM Get script directory
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
pushd "%SCRIPT_DIR%\.."
set "PROJECT_DIR=%CD%"
popd
set "BUILD_DIR=%PROJECT_DIR%\build"
set "TOOLS_DIR=%PROJECT_DIR%\tools"

REM Default options
set "MODE=graphics"
set "DEBUG=false"
set "NETWORK=true"
set "MEMORY=256M"
set "RUN_TESTS=false"
set "RUN_QEMU=true"
set "USE_UEFI=true"

REM Parse arguments
:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="--serial" (
    set "MODE=serial"
    shift
    goto :parse_args
)
if /i "%~1"=="--uefi" (
    set "USE_UEFI=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--direct" (
    set "USE_UEFI=false"
    shift
    goto :parse_args
)
if /i "%~1"=="--debug" (
    set "DEBUG=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--no-net" (
    set "NETWORK=false"
    shift
    goto :parse_args
)
if /i "%~1"=="--test" (
    set "RUN_TESTS=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--no-run" (
    set "RUN_QEMU=false"
    shift
    goto :parse_args
)
if /i "%~1"=="--memory" (
    set "MEMORY=%~2"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--help" goto :show_help
if /i "%~1"=="-h" goto :show_help
echo [ERROR] Unknown option: %~1
goto :show_help
:done_args

call :print_banner
goto :main

REM ============================================================================
REM Functions
REM ============================================================================

:print_banner
echo.
echo   ╦  ╦┬┌─┐┌─┐┬─┐╔═╗╔═╗
echo   ╚╗╔╝│├─┘├┤ ├┬┘║ ║╚═╗
echo    ╚╝ ┴┴  └─┘┴└─╚═╝╚═╝
echo.
echo   Build ^& Run Script v1.0 (Windows)
echo.
exit /b 0

:show_help
echo Usage: %~nx0 [options]
echo.
echo Options:
echo   --serial    Run in serial-only mode (no graphics window)
echo   --direct    Boot kernel directly (bypass VBoot bootloader)
echo   --debug     Enable GDB debugging (QEMU waits on port 1234)
echo   --no-net    Disable networking
echo   --test      Run tests before launching QEMU
echo   --no-run    Do not launch QEMU (build/test only)
echo   --memory N  Set memory size (default: 256M)
echo   --help      Show this help message
echo.
echo Examples:
echo   %~nx0                    Build and run with VBoot bootloader + GUI
echo   %~nx0 --serial           Build and run in terminal only (no GUI)
echo   %~nx0 --direct           Bypass bootloader, load kernel directly
echo   %~nx0 --test             Build, run tests, then launch QEMU
echo   %~nx0 --no-run           Build only, don't launch QEMU
echo.
echo Boot Modes:
echo   UEFI (default): QEMU runs UEFI firmware which loads VBoot bootloader
echo   Direct (--direct): QEMU loads kernel.sys directly at 0x40000000
echo.
echo Prerequisites (install via chocolatey or manually):
echo   choco install qemu cmake llvm
echo   - QEMU for Windows (qemu-system-aarch64)
echo   - CMake
echo   - LLVM/Clang with lld
echo   - AArch64 cross-toolchain (aarch64-elf-* binutils)
echo.
exit /b 0

:print_step
echo ==^> %~1
exit /b 0

:print_success
echo [OK] %~1
exit /b 0

:print_error
echo [ERROR] %~1
exit /b 0

:print_warning
echo [WARN] %~1
exit /b 0

REM ============================================================================
REM Main Script
REM ============================================================================

:main

REM Check for QEMU
call :print_step "Checking prerequisites..."

set "QEMU="
where qemu-system-aarch64 >nul 2>&1
if %ERRORLEVEL%==0 (
    for /f "delims=" %%i in ('where qemu-system-aarch64') do set "QEMU=%%i"
)

if "%QEMU%"=="" (
    REM Check common installation paths
    if exist "C:\Program Files\qemu\qemu-system-aarch64.exe" (
        set "QEMU=C:\Program Files\qemu\qemu-system-aarch64.exe"
    ) else if exist "%USERPROFILE%\scoop\apps\qemu\current\qemu-system-aarch64.exe" (
        set "QEMU=%USERPROFILE%\scoop\apps\qemu\current\qemu-system-aarch64.exe"
    )
)

if "%QEMU%"=="" (
    call :print_error "qemu-system-aarch64 not found!"
    echo Install with: choco install qemu
    echo Or download from: https://www.qemu.org/download/#windows
    exit /b 1
)
call :print_success "Found QEMU: %QEMU%"

REM Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    call :print_error "CMake not found!"
    echo Install with: choco install cmake
    exit /b 1
)
call :print_success "Found CMake"

REM Check for Clang
where clang >nul 2>&1
if %ERRORLEVEL% neq 0 (
    call :print_error "Clang not found!"
    echo Install with: choco install llvm
    exit /b 1
)
call :print_success "Found Clang"

REM Check for AArch64 cross-linker
set "CROSS_LD="
where aarch64-elf-ld >nul 2>&1
if %ERRORLEVEL%==0 (
    for /f "delims=" %%i in ('where aarch64-elf-ld') do set "CROSS_LD=%%i"
)
if "%CROSS_LD%"=="" (
    where aarch64-none-elf-ld >nul 2>&1
    if %ERRORLEVEL%==0 (
        for /f "delims=" %%i in ('where aarch64-none-elf-ld') do set "CROSS_LD=%%i"
    )
)
if "%CROSS_LD%"=="" (
    call :print_warning "AArch64 cross-linker not found!"
    echo You may need to install ARM GNU Toolchain from:
    echo https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
    echo Or use MSYS2: pacman -S mingw-w64-x86_64-aarch64-none-elf-gcc
)
if not "%CROSS_LD%"=="" (
    call :print_success "Found cross-linker: %CROSS_LD%"
)

REM Find UEFI firmware
set "UEFI_FW="
if "%USE_UEFI%"=="true" (
    REM Check common locations for UEFI firmware
    if exist "C:\Program Files\qemu\share\edk2-aarch64-code.fd" (
        set "UEFI_FW=C:\Program Files\qemu\share\edk2-aarch64-code.fd"
    ) else if exist "%QEMU%\..\share\edk2-aarch64-code.fd" (
        set "UEFI_FW=%QEMU%\..\share\edk2-aarch64-code.fd"
    ) else if exist "%TOOLS_DIR%\QEMU_EFI.fd" (
        set "UEFI_FW=%TOOLS_DIR%\QEMU_EFI.fd"
    )

    if "!UEFI_FW!"=="" (
        call :print_warning "UEFI firmware not found!"
        echo Download QEMU_EFI.fd and place in: %TOOLS_DIR%\
        echo Or install QEMU with EDK2 support
        echo Falling back to direct boot mode
        set "USE_UEFI=false"
    ) else (
        call :print_success "Found UEFI firmware: !UEFI_FW!"
    )
)

REM Clean build directory
call :print_step "Cleaning build directory..."
if exist "%BUILD_DIR%" rd /s /q "%BUILD_DIR%"
call :print_success "Clean complete"

REM Configure CMake
call :print_step "Configuring CMake..."
cmake -B "%BUILD_DIR%" -S "%PROJECT_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%PROJECT_DIR%\cmake\aarch64-clang-toolchain.cmake" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if %ERRORLEVEL% neq 0 (
    call :print_error "CMake configuration failed!"
    exit /b 1
)
call :print_success "Configuration complete"

REM Build
call :print_step "Building ViperDOS..."
cmake --build "%BUILD_DIR%" --parallel
if %ERRORLEVEL% neq 0 (
    call :print_error "Build failed!"
    exit /b 1
)
call :print_success "Build complete"

REM Check for required files
if not exist "%BUILD_DIR%\kernel.sys" (
    call :print_error "Kernel not found at %BUILD_DIR%\kernel.sys"
    exit /b 1
)
if not exist "%BUILD_DIR%\vinit.sys" (
    call :print_error "vinit not found at %BUILD_DIR%\vinit.sys"
    exit /b 1
)
if not exist "%BUILD_DIR%\hello.prg" (
    call :print_warning "hello.prg not found (spawn test program)"
)

REM Build tools if needed
call :print_step "Building tools..."
if exist "%TOOLS_DIR%\mkfs.viperfs.cpp" (
    if not exist "%TOOLS_DIR%\mkfs.viperfs.exe" (
        echo   Compiling mkfs.viperfs...
        cl /std:c++17 /O2 /Fe:"%TOOLS_DIR%\mkfs.viperfs.exe" "%TOOLS_DIR%\mkfs.viperfs.cpp" >nul 2>&1
        if %ERRORLEVEL% neq 0 (
            clang++ -std=c++17 -O2 -o "%TOOLS_DIR%\mkfs.viperfs.exe" "%TOOLS_DIR%\mkfs.viperfs.cpp"
        )
    )
)
if exist "%TOOLS_DIR%\gen_roots_der.cpp" (
    if not exist "%TOOLS_DIR%\gen_roots_der.exe" (
        echo   Compiling gen_roots_der...
        cl /std:c++17 /O2 /Fe:"%TOOLS_DIR%\gen_roots_der.exe" "%TOOLS_DIR%\gen_roots_der.cpp" >nul 2>&1
        if %ERRORLEVEL% neq 0 (
            clang++ -std=c++17 -O2 -o "%TOOLS_DIR%\gen_roots_der.exe" "%TOOLS_DIR%\gen_roots_der.cpp"
        )
    )
)
call :print_success "Tools ready"

REM Generate certificate bundle
call :print_step "Generating certificate bundle..."
if exist "%TOOLS_DIR%\gen_roots_der.exe" (
    "%TOOLS_DIR%\gen_roots_der.exe" "%BUILD_DIR%\roots.der"
    call :print_success "Certificate bundle created"
) else (
    call :print_warning "gen_roots_der not found, skipping certificate bundle"
)

REM Create disk images
call :print_step "Creating disk images..."
if exist "%TOOLS_DIR%\mkfs.viperfs.exe" (
    REM sys.img - System disk (2MB)
    set "SYS_ARGS=%BUILD_DIR%\sys.img 2 %BUILD_DIR%\vinit.sys"
    for %%s in (blkd netd fsd consoled displayd) do (
        if exist "%BUILD_DIR%\%%s.sys" (
            set "SYS_ARGS=!SYS_ARGS! --add %BUILD_DIR%\%%s.sys:%%s.sys"
        )
    )
    "%TOOLS_DIR%\mkfs.viperfs.exe" !SYS_ARGS!
    call :print_success "sys.img created (system disk)"

    REM user.img - User disk (8MB)
    set "USER_ARGS=%BUILD_DIR%\user.img 8 --mkdir c --mkdir certs --mkdir s --mkdir t"
    for %%p in (hello fsd_smoke netd_smoke tls_smoke edit sftp ssh ping fsinfo netstat sysinfo devices mathtest faulttest_null faulttest_illegal hello_gui taskbar) do (
        if exist "%BUILD_DIR%\%%p.prg" (
            set "USER_ARGS=!USER_ARGS! --add %BUILD_DIR%\%%p.prg:c/%%p.prg"
        )
    )
    if exist "%BUILD_DIR%\roots.der" (
        set "USER_ARGS=!USER_ARGS! --add %BUILD_DIR%\roots.der:certs/roots.der"
    )
    "%TOOLS_DIR%\mkfs.viperfs.exe" !USER_ARGS!
    call :print_success "user.img created (user disk)"

    REM Legacy compatibility
    copy /y "%BUILD_DIR%\sys.img" "%BUILD_DIR%\disk.img" >nul
    call :print_success "disk.img created (legacy alias)"
) else (
    call :print_warning "mkfs.viperfs not found, using existing disk images"
)

REM ESP Image Creation (for UEFI boot)
if "%USE_UEFI%"=="true" (
    call :print_step "Creating ESP image for UEFI boot..."

    if not exist "%BUILD_DIR%\BOOTAA64.EFI" (
        call :print_error "VBoot bootloader not found at %BUILD_DIR%\BOOTAA64.EFI"
        call :print_warning "Falling back to direct boot mode"
        set "USE_UEFI=false"
        goto :skip_esp
    )

    REM Check for mtools
    where mmd >nul 2>&1
    if %ERRORLEVEL% neq 0 (
        call :print_warning "mtools not found - ESP creation requires mtools"
        echo Install mtools via MSYS2 or use direct boot mode (--direct)
        call :print_warning "Falling back to direct boot mode"
        set "USE_UEFI=false"
        goto :skip_esp
    )

    set "ESP_IMG=%BUILD_DIR%\esp.img"

    REM Create 40MB disk image
    fsutil file createnew "!ESP_IMG!" 41943040 >nul 2>&1
    if %ERRORLEVEL% neq 0 (
        REM Alternative: create with dd-like tool or PowerShell
        powershell -Command "$f=[IO.File]::Create('!ESP_IMG!');$f.SetLength(41943040);$f.Close()"
    )

    REM Note: Full GPT/FAT32 creation on Windows is complex without additional tools
    REM For now, warn user they may need WSL or MSYS2 for full UEFI boot support
    call :print_warning "ESP image creation on Windows requires additional tools"
    echo For full UEFI boot support, consider using WSL or MSYS2
    echo Alternatively, use --direct flag to bypass bootloader
    set "USE_UEFI=false"
)
:skip_esp

REM Run tests
if "%RUN_TESTS%"=="true" (
    call :print_step "Running tests..."
    ctest --test-dir "%BUILD_DIR%" --output-on-failure
    if %ERRORLEVEL% neq 0 (
        call :print_error "Some tests failed!"
        exit /b 1
    )
    call :print_success "All tests passed"
)

REM Exit if not running QEMU
if "%RUN_QEMU%"=="false" (
    call :print_success "Build complete (QEMU launch skipped)"
    exit /b 0
)

REM Build QEMU command
call :print_step "Starting QEMU..."
echo.

set "QEMU_OPTS=-machine virt -cpu cortex-a72 -m %MEMORY%"

REM Boot method selection
if "%USE_UEFI%"=="true" (
    echo   Boot Mode: UEFI (VBoot bootloader)
    echo   UEFI Firmware: %UEFI_FW%
    set "QEMU_OPTS=!QEMU_OPTS! -bios "%UEFI_FW%""
    set "QEMU_OPTS=!QEMU_OPTS! -drive file=%BUILD_DIR%\esp.img,if=none,format=raw,id=esp"
    set "QEMU_OPTS=!QEMU_OPTS! -device virtio-blk-device,drive=esp,bootindex=0"
    echo   ESP Image: %BUILD_DIR%\esp.img
) else (
    echo   Boot Mode: Direct kernel load
    set "QEMU_OPTS=!QEMU_OPTS! -kernel "%BUILD_DIR%\kernel.sys""
)

REM System disk
if exist "%BUILD_DIR%\sys.img" (
    if "%USE_UEFI%"=="true" (
        set "QEMU_OPTS=!QEMU_OPTS! -drive file=%BUILD_DIR%\sys.img,if=none,format=raw,id=disk1"
        set "QEMU_OPTS=!QEMU_OPTS! -device virtio-blk-device,drive=disk1"
    ) else (
        set "QEMU_OPTS=!QEMU_OPTS! -drive file=%BUILD_DIR%\sys.img,if=none,format=raw,id=disk0"
        set "QEMU_OPTS=!QEMU_OPTS! -device virtio-blk-device,drive=disk0"
    )
    echo   System Disk: %BUILD_DIR%\sys.img
)

REM User disk
if exist "%BUILD_DIR%\user.img" (
    if "%USE_UEFI%"=="true" (
        set "QEMU_OPTS=!QEMU_OPTS! -drive file=%BUILD_DIR%\user.img,if=none,format=raw,id=disk2"
        set "QEMU_OPTS=!QEMU_OPTS! -device virtio-blk-device,drive=disk2"
    ) else (
        set "QEMU_OPTS=!QEMU_OPTS! -drive file=%BUILD_DIR%\user.img,if=none,format=raw,id=disk1"
        set "QEMU_OPTS=!QEMU_OPTS! -device virtio-blk-device,drive=disk1"
    )
    echo   User Disk: %BUILD_DIR%\user.img
)

REM RNG device
set "QEMU_OPTS=!QEMU_OPTS! -device virtio-rng-device"
echo   RNG: virtio-rng (hardware entropy)

REM Network options
if "%NETWORK%"=="true" (
    set "QEMU_OPTS=!QEMU_OPTS! -netdev user,id=net0"
    set "QEMU_OPTS=!QEMU_OPTS! -device virtio-net-device,netdev=net0"
    echo   Network: virtio-net (10.0.2.15)

    if exist "%BUILD_DIR%\netd.sys" (
        set "QEMU_OPTS=!QEMU_OPTS! -netdev user,id=net1"
        set "QEMU_OPTS=!QEMU_OPTS! -device virtio-net-device,netdev=net1"
        echo   Extra Network: virtio-net (net1)
    )
)

REM Display options
if "%MODE%"=="serial" (
    set "QEMU_OPTS=!QEMU_OPTS! -nographic"
    echo   Display: Serial only (Ctrl+A X to exit)
) else (
    set "QEMU_OPTS=!QEMU_OPTS! -device ramfb"
    set "QEMU_OPTS=!QEMU_OPTS! -device virtio-keyboard-device"
    set "QEMU_OPTS=!QEMU_OPTS! -device virtio-mouse-device"
    set "QEMU_OPTS=!QEMU_OPTS! -serial stdio"
    echo   Display: Graphics mode (ramfb + mouse + keyboard)
    echo   Tip: Run 'run /c/displayd.sys' then 'run /c/hello_gui.prg' to test GUI
)

REM Debug options
if "%DEBUG%"=="true" (
    set "QEMU_OPTS=!QEMU_OPTS! -s -S"
    echo   Debug: Waiting for GDB on localhost:1234
    echo.
    echo   Connect with: gdb %BUILD_DIR%\kernel.sys -ex "target remote :1234"
)

set "QEMU_OPTS=!QEMU_OPTS! -no-reboot"

echo.
echo   Memory: %MEMORY%
echo.

REM Run QEMU
"%QEMU%" !QEMU_OPTS!

endlocal
exit /b 0
