#!/bin/bash
# ViperOS Complete Build and Run Script
# Usage: ./build_viperos.sh [options]
#   --serial    Run in serial-only mode (no graphics)
#   --direct    Boot kernel directly (bypass VBoot bootloader)
#   --debug     Enable GDB debugging (wait on port 1234)
#   --no-net    Disable networking
#   --test      Run tests before launching QEMU
#   --no-run    Do not launch QEMU (build/test only)
#   --help      Show this help

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
TOOLS_DIR="$PROJECT_DIR/tools"

# Default options
MODE="graphics"
DEBUG=false
NETWORK=true
MEMORY="256M"
RUN_TESTS=false
RUN_QEMU=true
USE_UEFI=true

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_banner() {
    echo -e "${GREEN}"
    echo "  ╦  ╦┬┌─┐┌─┐┬─┐╔═╗╔═╗"
    echo "  ╚╗╔╝│├─┘├┤ ├┬┘║ ║╚═╗"
    echo "   ╚╝ ┴┴  └─┘┴└─╚═╝╚═╝"
    echo -e "${NC}"
    echo "  Build & Run Script v1.0"
    echo ""
}

print_step() {
    echo -e "${BLUE}==>${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}!${NC} $1"
}

show_help() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --serial    Run in serial-only mode (no graphics window)"
    echo "  --direct    Boot kernel directly (bypass VBoot bootloader)"
    echo "  --debug     Enable GDB debugging (QEMU waits on port 1234)"
    echo "  --no-net    Disable networking"
    echo "  --test      Run tests before launching QEMU"
    echo "  --no-run    Do not launch QEMU (build/test only)"
    echo "  --memory N  Set memory size (default: 256M)"
    echo "  --help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build and run with VBoot bootloader + GUI"
    echo "  $0 --serial           # Build and run in terminal only (no GUI)"
    echo "  $0 --direct           # Bypass bootloader, load kernel directly"
    echo "  $0 --test             # Build, run tests, then launch QEMU"
    echo "  $0 --no-run           # Build only, don't launch QEMU"
    echo ""
    echo "Boot Modes:"
    echo "  UEFI (default): QEMU runs UEFI firmware which loads VBoot bootloader"
    echo "  Direct (--direct): QEMU loads kernel.sys directly at 0x40000000"
    echo ""
}

# Find UEFI firmware for AArch64
find_uefi_firmware() {
    local paths=(
        # macOS Homebrew locations
        "/opt/homebrew/share/qemu/edk2-aarch64-code.fd"
        "/usr/local/share/qemu/edk2-aarch64-code.fd"
        # Linux package locations
        "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
        "/usr/share/OVMF/QEMU_EFI.fd"
        "/usr/share/edk2/aarch64/QEMU_EFI.fd"
        # Local tools directory
        "$TOOLS_DIR/QEMU_EFI.fd"
    )
    for p in "${paths[@]}"; do
        if [[ -f "$p" ]]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --serial)
            MODE="serial"
            shift
            ;;
        --uefi)
            USE_UEFI=true
            shift
            ;;
        --direct)
            USE_UEFI=false
            shift
            ;;
        --debug)
            DEBUG=true
            shift
            ;;
        --no-net)
            NETWORK=false
            shift
            ;;
        --test)
            RUN_TESTS=true
            shift
            ;;
        --no-run)
            RUN_QEMU=false
            shift
            ;;
        --memory)
            MEMORY="$2"
            shift 2
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

print_banner

# ============================================================================
# Prerequisites Installation
# ============================================================================

detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ -f /etc/debian_version ]]; then
        echo "debian"
    elif [[ -f /etc/redhat-release ]]; then
        echo "redhat"
    else
        echo "unknown"
    fi
}

install_prerequisites() {
    local os=$(detect_os)
    local missing=()

    print_step "Checking prerequisites..."

    # Check each required tool
    if ! command -v qemu-system-aarch64 &> /dev/null; then
        missing+=("qemu")
    fi

    if ! command -v cmake &> /dev/null; then
        missing+=("cmake")
    fi

    if ! command -v aarch64-elf-ld &> /dev/null; then
        missing+=("aarch64-elf-binutils")
    fi

    # For UEFI boot, we need additional tools
    if [[ "$USE_UEFI" == true ]]; then
        if ! command -v sgdisk &> /dev/null; then
            missing+=("gptfdisk")
        fi

        if ! command -v mmd &> /dev/null; then
            missing+=("mtools")
        fi

        # Check for LLVM clang (not Apple clang) and lld
        if [[ ! -x "/opt/homebrew/opt/llvm/bin/clang" ]] && [[ "$os" == "macos" ]]; then
            missing+=("llvm")
        fi

        if ! command -v lld-link &> /dev/null; then
            missing+=("lld")
        fi
    fi

    # If nothing is missing, return early
    if [[ ${#missing[@]} -eq 0 ]]; then
        print_success "All prerequisites installed"
        return 0
    fi

    print_warning "Missing prerequisites: ${missing[*]}"

    case $os in
        macos)
            # Check for Homebrew
            if ! command -v brew &> /dev/null; then
                print_error "Homebrew not found. Please install from https://brew.sh"
                exit 1
            fi

            print_step "Installing missing packages via Homebrew..."
            for pkg in "${missing[@]}"; do
                case $pkg in
                    qemu)
                        brew install qemu
                        ;;
                    cmake)
                        brew install cmake
                        ;;
                    aarch64-elf-binutils)
                        brew install aarch64-elf-binutils
                        ;;
                    gptfdisk)
                        brew install gptfdisk
                        ;;
                    mtools)
                        brew install mtools
                        ;;
                    llvm)
                        brew install llvm
                        ;;
                    lld)
                        brew install lld
                        ;;
                esac
            done
            ;;

        debian)
            print_step "Installing missing packages via apt..."
            local apt_pkgs=()
            for pkg in "${missing[@]}"; do
                case $pkg in
                    qemu)
                        apt_pkgs+=("qemu-system-arm")
                        ;;
                    cmake)
                        apt_pkgs+=("cmake")
                        ;;
                    aarch64-elf-binutils)
                        apt_pkgs+=("binutils-aarch64-linux-gnu")
                        ;;
                    gptfdisk)
                        apt_pkgs+=("gdisk")
                        ;;
                    mtools)
                        apt_pkgs+=("mtools")
                        ;;
                    llvm|lld)
                        apt_pkgs+=("clang" "lld")
                        ;;
                esac
            done

            if [[ ${#apt_pkgs[@]} -gt 0 ]]; then
                sudo apt-get update
                sudo apt-get install -y "${apt_pkgs[@]}"
            fi
            ;;

        *)
            print_error "Unsupported OS. Please install manually:"
            echo "  Required: qemu-system-aarch64, cmake, aarch64-elf-ld"
            if [[ "$USE_UEFI" == true ]]; then
                echo "  For UEFI: sgdisk, mtools, clang, lld-link"
            fi
            exit 1
            ;;
    esac

    print_success "Prerequisites installed"
}

# Run prerequisite check and installation
install_prerequisites

# ============================================================================
# Tool Verification
# ============================================================================

# Check for QEMU
QEMU=""
if command -v qemu-system-aarch64 &> /dev/null; then
    QEMU="qemu-system-aarch64"
elif [[ -x "/opt/homebrew/opt/qemu/bin/qemu-system-aarch64" ]]; then
    QEMU="/opt/homebrew/opt/qemu/bin/qemu-system-aarch64"
elif [[ -x "/usr/local/bin/qemu-system-aarch64" ]]; then
    QEMU="/usr/local/bin/qemu-system-aarch64"
else
    print_error "qemu-system-aarch64 not found!"
    echo "Install with: brew install qemu (macOS) or apt install qemu-system-arm (Linux)"
    exit 1
fi
print_success "Found QEMU: $QEMU"

# Check for Clang and cross-linker
if ! command -v clang &> /dev/null; then
    print_error "Clang not found!"
    echo "Install with: brew install llvm (macOS) or apt install clang (Linux)"
    exit 1
fi
print_success "Found Clang: $(clang --version | head -1)"

if ! command -v aarch64-elf-ld &> /dev/null; then
    print_error "AArch64 cross-linker not found!"
    echo "Install with: brew install aarch64-elf-binutils (macOS)"
    exit 1
fi
print_success "Found cross-linker: $(which aarch64-elf-ld)"

# Always do a clean build
print_step "Cleaning build directory..."
rm -rf "$BUILD_DIR"
print_success "Clean complete"

# Configure CMake with Clang toolchain
print_step "Configuring CMake (Clang)..."
cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$PROJECT_DIR/cmake/aarch64-clang-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
print_success "Configuration complete"

# Build
print_step "Building ViperOS..."
cmake --build "$BUILD_DIR" --parallel
print_success "Build complete"

# Check for required files
if [[ ! -f "$BUILD_DIR/kernel.sys" ]]; then
    print_error "Kernel not found at $BUILD_DIR/kernel.sys"
    exit 1
fi

if [[ ! -f "$BUILD_DIR/vinit.sys" ]]; then
    print_error "vinit not found at $BUILD_DIR/vinit.sys"
    exit 1
fi

if [[ ! -f "$BUILD_DIR/hello.prg" ]]; then
    print_warning "hello.prg not found at $BUILD_DIR/hello.prg (spawn test program)"
fi

# Build tools if needed
print_step "Building tools..."
if [[ ! -x "$TOOLS_DIR/mkfs.viperfs" ]] || [[ "$TOOLS_DIR/mkfs.viperfs.cpp" -nt "$TOOLS_DIR/mkfs.viperfs" ]]; then
    echo "  Compiling mkfs.viperfs..."
    c++ -std=c++17 -O2 -o "$TOOLS_DIR/mkfs.viperfs" "$TOOLS_DIR/mkfs.viperfs.cpp"
fi
if [[ ! -x "$TOOLS_DIR/gen_roots_der" ]] || [[ "$TOOLS_DIR/gen_roots_der.cpp" -nt "$TOOLS_DIR/gen_roots_der" ]]; then
    echo "  Compiling gen_roots_der..."
    c++ -std=c++17 -O2 -o "$TOOLS_DIR/gen_roots_der" "$TOOLS_DIR/gen_roots_der.cpp"
fi
print_success "Tools ready"

# Generate roots.der certificate bundle
print_step "Generating certificate bundle..."
if [[ -x "$TOOLS_DIR/gen_roots_der" ]]; then
    "$TOOLS_DIR/gen_roots_der" "$BUILD_DIR/roots.der"
    print_success "Certificate bundle created"
else
    print_warning "gen_roots_der not found, skipping certificate bundle"
fi

# =============================================================================
# Two-Disk Architecture
# =============================================================================
# sys.img (disk0): System disk - kernel access via VirtIO-blk
#   Contains vinit.sys and all system servers at root level
# user.img (disk1): User disk - blkd/fsd access
#   Contains user programs in /c/, certificates in /certs/, etc.
#
# This allows the system to boot to a functional shell even if the user
# disk is missing (graceful degradation to "system-only" mode).

print_step "Creating disk images..."
if [[ -x "$TOOLS_DIR/mkfs.viperfs" ]]; then
    # -------------------------------------------------------------------------
    # sys.img - System disk (2MB)
    # -------------------------------------------------------------------------
    SYS_ARGS=("$BUILD_DIR/sys.img" 2 "$BUILD_DIR/vinit.sys")
    for srv in blkd netd fsd consoled displayd; do
        [[ -f "$BUILD_DIR/${srv}.sys" ]] && SYS_ARGS+=(--add "$BUILD_DIR/${srv}.sys:${srv}.sys")
    done
    "$TOOLS_DIR/mkfs.viperfs" "${SYS_ARGS[@]}"
    print_success "sys.img created (system disk)"

    # -------------------------------------------------------------------------
    # user.img - User disk (8MB)
    # -------------------------------------------------------------------------
    USER_ARGS=("$BUILD_DIR/user.img" 8 --mkdir c --mkdir certs --mkdir s --mkdir t)
    # Add user programs to /c/
    for prg in hello fsd_smoke netd_smoke tls_smoke edit sftp ssh ping \
               fsinfo netstat sysinfo devices mathtest faulttest_null \
               faulttest_illegal hello_gui taskbar; do
        [[ -f "$BUILD_DIR/${prg}.prg" ]] && USER_ARGS+=(--add "$BUILD_DIR/${prg}.prg:c/${prg}.prg")
    done
    # Add certificate bundle
    [[ -f "$BUILD_DIR/roots.der" ]] && USER_ARGS+=(--add "$BUILD_DIR/roots.der:certs/roots.der")
    "$TOOLS_DIR/mkfs.viperfs" "${USER_ARGS[@]}"
    print_success "user.img created (user disk)"

    # Legacy compatibility: disk.img alias for sys.img
    cp -f "$BUILD_DIR/sys.img" "$BUILD_DIR/disk.img"
    print_success "disk.img created (legacy alias)"
else
    print_warning "mkfs.viperfs not found, using existing disk images"
fi

# =============================================================================
# ESP Image Creation (for UEFI boot)
# =============================================================================
# When --uefi is specified, we create a FAT32 EFI System Partition containing:
#   /EFI/BOOT/BOOTAA64.EFI  - The VBoot bootloader
#   /viperos/kernel.sys     - The kernel

if [[ "$USE_UEFI" == true ]]; then
    print_step "Creating ESP image for UEFI boot..."

    # Check for required files
    if [[ ! -f "$BUILD_DIR/BOOTAA64.EFI" ]]; then
        print_error "VBoot bootloader not found at $BUILD_DIR/BOOTAA64.EFI"
        print_error "Build failed to produce the UEFI bootloader"
        exit 1
    fi

    # Check for mtools (required for FAT manipulation)
    if ! command -v mmd &> /dev/null || ! command -v mcopy &> /dev/null; then
        print_error "mtools not found (required for ESP image creation)"
        echo "Install with: brew install mtools (macOS) or apt install mtools (Linux)"
        exit 1
    fi

    # Find UEFI firmware
    if UEFI_FW=$(find_uefi_firmware); then
        print_success "Found UEFI firmware: $UEFI_FW"
    else
        print_error "UEFI firmware not found!"
        echo "Install with: brew install qemu (macOS, includes EDK2)"
        echo "Or: apt install qemu-efi-aarch64 (Linux)"
        echo "Or download QEMU_EFI.fd to $TOOLS_DIR/"
        exit 1
    fi

    # Create GPT-partitioned disk with EFI System Partition
    # UEFI requires a proper GPT disk with an ESP (type EF00)
    ESP_IMG="$BUILD_DIR/esp.img"

    # Check if ESP image is already up-to-date (skip slow recreation)
    ESP_NEEDS_UPDATE=false
    if [[ ! -f "$ESP_IMG" ]]; then
        ESP_NEEDS_UPDATE=true
    elif [[ "$BUILD_DIR/BOOTAA64.EFI" -nt "$ESP_IMG" ]]; then
        ESP_NEEDS_UPDATE=true
    elif [[ "$BUILD_DIR/kernel.sys" -nt "$ESP_IMG" ]]; then
        ESP_NEEDS_UPDATE=true
    fi

    if [[ "$ESP_NEEDS_UPDATE" == false ]]; then
        print_success "esp.img is up-to-date (skipping recreation)"
    else
    # Create 40MB disk image (GPT needs ~34 sectors at start/end)
    dd if=/dev/zero of="$ESP_IMG" bs=1M count=40 status=none

    # Check for sgdisk (GPT partitioning tool)
    if ! command -v sgdisk &> /dev/null; then
        print_error "sgdisk not found (required for GPT partitioning)"
        echo "Install with: brew install gptfdisk (macOS) or apt install gdisk (Linux)"
        exit 1
    fi

    # Create GPT with one EFI System Partition
    # -n 1:2048:0 = partition 1, starts at sector 2048, fills disk
    # -t 1:EF00 = partition type EFI System Partition
    sgdisk -Z "$ESP_IMG" > /dev/null 2>&1  # Zap existing
    sgdisk -n 1:2048:0 -t 1:EF00 "$ESP_IMG" > /dev/null 2>&1

    # Get partition offset (sector 2048 * 512 bytes = 1048576 = 1MB)
    PART_OFFSET=1048576

    # Format the partition as FAT32
    # mtools needs MTOOLS_SKIP_CHECK to ignore partition table
    export MTOOLS_SKIP_CHECK=1

    # Create a temporary partition image, format it, then copy back
    PART_SIZE=$((40*1024*1024 - PART_OFFSET - 17*512))  # Subtract GPT backup
    ESP_PART="$BUILD_DIR/esp_part.img"
    dd if=/dev/zero of="$ESP_PART" bs=1M count=39 status=none

    # Format the partition image as FAT32
    if command -v mkfs.vfat &> /dev/null; then
        mkfs.vfat -F 32 "$ESP_PART" > /dev/null 2>&1
    elif command -v newfs_msdos &> /dev/null; then
        newfs_msdos -F 32 "$ESP_PART" > /dev/null 2>&1
    else
        print_error "No FAT32 formatting tool found"
        exit 1
    fi

    # Create directory structure and copy files
    mmd -i "$ESP_PART" ::/EFI
    mmd -i "$ESP_PART" ::/EFI/BOOT
    mmd -i "$ESP_PART" ::/viperos
    mcopy -i "$ESP_PART" "$BUILD_DIR/BOOTAA64.EFI" ::/EFI/BOOT/
    mcopy -i "$ESP_PART" "$BUILD_DIR/kernel.sys" ::/viperos/

    # Create startup.nsh to auto-boot when UEFI shell runs
    # This provides a fallback if automatic boot detection fails
    echo '\EFI\BOOT\BOOTAA64.EFI' > "$BUILD_DIR/startup.nsh"
    mcopy -i "$ESP_PART" "$BUILD_DIR/startup.nsh" ::/
    rm -f "$BUILD_DIR/startup.nsh"

    # Copy partition back into the GPT disk image at the correct offset
    dd if="$ESP_PART" of="$ESP_IMG" bs=512 seek=2048 conv=notrunc status=none
    rm -f "$ESP_PART"

    print_success "esp.img created (40MB GPT disk with EFI System Partition)"
    echo "  Contents:"
    echo "    /EFI/BOOT/BOOTAA64.EFI (VBoot bootloader)"
    echo "    /viperos/kernel.sys (kernel)"
    fi  # end ESP_NEEDS_UPDATE else
fi

# Run tests (after disk image is created so tests can use it)
if [[ "$RUN_TESTS" == true ]]; then
    print_step "Running tests..."
    if ctest --test-dir "$BUILD_DIR" --output-on-failure; then
        print_success "All tests passed"
    else
        print_error "Some tests failed!"
        exit 1
    fi
fi

# Exit early if we're only building/testing.
if [[ "$RUN_QEMU" == false ]]; then
    print_success "Build complete (QEMU launch skipped)"
    exit 0
fi

# Build QEMU command
print_step "Starting QEMU..."
echo ""

QEMU_OPTS=(
    -machine virt
    -cpu cortex-a72
    -m "$MEMORY"
)

# Boot method selection
if [[ "$USE_UEFI" == true ]]; then
    # UEFI boot via VBoot bootloader
    echo "  Boot Mode: UEFI (VBoot bootloader)"
    echo "  UEFI Firmware: $UEFI_FW"

    QEMU_OPTS+=(
        -bios "$UEFI_FW"
        -drive "file=$BUILD_DIR/esp.img,if=none,format=raw,id=esp"
        -device virtio-blk-device,drive=esp,bootindex=0
    )
    echo "  ESP Image: $BUILD_DIR/esp.img"
else
    # Direct kernel load (default, bypasses bootloader)
    echo "  Boot Mode: Direct kernel load"
    QEMU_OPTS+=(-kernel "$BUILD_DIR/kernel.sys")
fi

# Two-disk architecture:
#   disk0/1 (sys.img): System disk - kernel access
#   disk1/2 (user.img): User disk - blkd/fsd access
# Note: disk indices shift by 1 when ESP is present

# System disk (kernel access via VirtIO-blk)
if [[ -f "$BUILD_DIR/sys.img" ]]; then
    if [[ "$USE_UEFI" == true ]]; then
        QEMU_OPTS+=(
            -drive "file=$BUILD_DIR/sys.img,if=none,format=raw,id=disk1"
            -device virtio-blk-device,drive=disk1
        )
    else
        QEMU_OPTS+=(
            -drive "file=$BUILD_DIR/sys.img,if=none,format=raw,id=disk0"
            -device virtio-blk-device,drive=disk0
        )
    fi
    echo "  System Disk: $BUILD_DIR/sys.img"
fi

# User disk (blkd/fsd access via VirtIO-blk)
if [[ -f "$BUILD_DIR/user.img" ]]; then
    if [[ "$USE_UEFI" == true ]]; then
        QEMU_OPTS+=(
            -drive "file=$BUILD_DIR/user.img,if=none,format=raw,id=disk2"
            -device virtio-blk-device,drive=disk2
        )
    else
        QEMU_OPTS+=(
            -drive "file=$BUILD_DIR/user.img,if=none,format=raw,id=disk1"
            -device virtio-blk-device,drive=disk1
        )
    fi
    echo "  User Disk: $BUILD_DIR/user.img"
fi

# RNG device (entropy source for TLS)
QEMU_OPTS+=(-device virtio-rng-device)
echo "  RNG: virtio-rng (hardware entropy)"

# Network options
if [[ "$NETWORK" == true ]]; then
    QEMU_OPTS+=(
        -netdev user,id=net0
        -device virtio-net-device,netdev=net0
    )
    echo "  Network: virtio-net (10.0.2.15)"

    # Dedicated microkernel NIC (for user-space netd)
    if [[ -f "$BUILD_DIR/netd.sys" ]]; then
        QEMU_OPTS+=(
            -netdev user,id=net1
            -device virtio-net-device,netdev=net1
        )
        echo "  Microkernel Network: virtio-net (net1)"
    fi
fi

# Display options
case "$MODE" in
    serial)
        QEMU_OPTS+=(-nographic)
        echo "  Display: Serial only (Ctrl+A X to exit)"
        ;;
    graphics)
        QEMU_OPTS+=(
            -device ramfb
            -device virtio-keyboard-device
            -device virtio-mouse-device
            -chardev stdio,id=ser0,mux=on,signal=off
            -serial chardev:ser0
        )
        echo "  Display: Graphics mode (ramfb + mouse + keyboard)"
        echo "  Tip: Run 'run /c/displayd.sys' then 'run /c/hello_gui.prg' to test GUI"
        ;;
esac

# Debug options
if [[ "$DEBUG" == true ]]; then
    QEMU_OPTS+=(-s -S)
    echo "  Debug: Waiting for GDB on localhost:1234"
    echo ""
    echo "  Connect with: gdb-multiarch $BUILD_DIR/kernel.sys -ex 'target remote :1234'"
fi

QEMU_OPTS+=(-no-reboot)

echo ""
echo "  Memory: $MEMORY"
echo ""

# Run QEMU
exec "$QEMU" "${QEMU_OPTS[@]}"
