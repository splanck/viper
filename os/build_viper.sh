#!/bin/bash
# ViperOS Complete Build and Run Script
# Usage: ./build_viper.sh [options]
#   --clean     Clean build directory first
#   --serial    Run in serial-only mode (no graphics)
#   --debug     Enable GDB debugging (wait on port 1234)
#   --no-net    Disable networking
#   --help      Show this help

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
TOOLS_DIR="$SCRIPT_DIR/tools"

# Default options
CLEAN=false
MODE="graphics"
DEBUG=false
NETWORK=true
MEMORY="128M"

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
    echo "  --clean     Clean build directory before building"
    echo "  --serial    Run in serial-only mode (no graphics window)"
    echo "  --debug     Enable GDB debugging (QEMU waits on port 1234)"
    echo "  --no-net    Disable networking"
    echo "  --memory N  Set memory size (default: 128M)"
    echo "  --help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build and run with graphics"
    echo "  $0 --serial           # Build and run in terminal only"
    echo "  $0 --clean --serial   # Clean build, run in terminal"
    echo ""
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=true
            shift
            ;;
        --serial)
            MODE="serial"
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

# Check for cross-compiler
if ! command -v aarch64-elf-gcc &> /dev/null && ! command -v aarch64-none-elf-gcc &> /dev/null; then
    print_warning "AArch64 cross-compiler not in PATH"
    echo "Make sure aarch64-elf-gcc or aarch64-none-elf-gcc is available"
fi

# Clean if requested
if [[ "$CLEAN" == true ]]; then
    print_step "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    print_success "Clean complete"
fi

# Configure CMake if needed
if [[ ! -f "$BUILD_DIR/Makefile" ]] && [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    print_step "Configuring CMake..."
    cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/aarch64-toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    print_success "Configuration complete"
fi

# Build
print_step "Building ViperOS..."
cmake --build "$BUILD_DIR" --parallel
print_success "Build complete"

# Check for required files
if [[ ! -f "$BUILD_DIR/kernel.elf" ]]; then
    print_error "Kernel not found at $BUILD_DIR/kernel.elf"
    exit 1
fi

if [[ ! -f "$BUILD_DIR/vinit.elf" ]]; then
    print_error "vinit not found at $BUILD_DIR/vinit.elf"
    exit 1
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

# Create/update disk image with directory structure
print_step "Creating disk image..."
if [[ -x "$TOOLS_DIR/mkfs.viperfs" ]]; then
    MKFS_ARGS=(
        "$BUILD_DIR/disk.img" 8
        "$BUILD_DIR/vinit.elf"
        --mkdir SYS
        --mkdir SYS/certs
    )
    # Add roots.der if it was generated
    if [[ -f "$BUILD_DIR/roots.der" ]]; then
        MKFS_ARGS+=(--add "$BUILD_DIR/roots.der:SYS/certs/roots.der")
    fi
    "$TOOLS_DIR/mkfs.viperfs" "${MKFS_ARGS[@]}"
    print_success "Disk image created"
else
    print_warning "mkfs.viperfs not found, using existing disk image"
fi

# Build QEMU command
print_step "Starting QEMU..."
echo ""

QEMU_OPTS=(
    -machine virt
    -cpu cortex-a72
    -m "$MEMORY"
    -kernel "$BUILD_DIR/kernel.elf"
)

# Disk options (use legacy virtio for compatibility)
if [[ -f "$BUILD_DIR/disk.img" ]]; then
    QEMU_OPTS+=(
        -drive "file=$BUILD_DIR/disk.img,if=none,format=raw,id=disk0"
        -device virtio-blk-device,drive=disk0
    )
    echo "  Disk: $BUILD_DIR/disk.img"
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
        echo "  Display: Graphics + Serial"
        ;;
esac

# Debug options
if [[ "$DEBUG" == true ]]; then
    QEMU_OPTS+=(-s -S)
    echo "  Debug: Waiting for GDB on localhost:1234"
    echo ""
    echo "  Connect with: gdb-multiarch $BUILD_DIR/kernel.elf -ex 'target remote :1234'"
fi

QEMU_OPTS+=(-no-reboot)

echo ""
echo "  Memory: $MEMORY"
echo ""

# Run QEMU
exec "$QEMU" "${QEMU_OPTS[@]}"
