#!/bin/bash
# ViperOS Complete Build and Run Script
# Usage: ./build_viper.sh [options]
#   --serial    Run in serial-only mode (no graphics)
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
    echo "  --debug     Enable GDB debugging (QEMU waits on port 1234)"
    echo "  --no-net    Disable networking"
    echo "  --test      Run tests before launching QEMU"
    echo "  --no-run    Do not launch QEMU (build/test only)"
    echo "  --memory N  Set memory size (default: 256M)"
    echo "  --help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build and run with GUI desktop"
    echo "  $0 --serial           # Build and run in terminal only (no GUI)"
    echo "  $0 --test             # Build, run tests, then launch QEMU"
    echo "  $0 --no-run           # Build only, don't launch QEMU"
    echo ""
    echo "GUI Desktop:"
    echo "  To start the GUI desktop, run these commands from the shell:"
    echo "    run /c/inputd.sys      # Start input server (mouse/keyboard)"
    echo "    run /c/displayd.sys    # Start display server (shows desktop)"
    echo "    run /c/hello_gui.prg   # Run a test GUI application"
    echo ""
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
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
    for srv in blkd netd fsd consoled inputd displayd; do
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
               faulttest_illegal hello_gui; do
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
    -kernel "$BUILD_DIR/kernel.sys"
)

# Two-disk architecture:
#   disk0 (sys.img): System disk - kernel access
#   disk1 (user.img): User disk - blkd/fsd access

# System disk (kernel access via VirtIO-blk)
if [[ -f "$BUILD_DIR/sys.img" ]]; then
    QEMU_OPTS+=(
        -drive "file=$BUILD_DIR/sys.img,if=none,format=raw,id=disk0"
        -device virtio-blk-device,drive=disk0
    )
    echo "  System Disk: $BUILD_DIR/sys.img"
fi

# User disk (blkd/fsd access via VirtIO-blk)
if [[ -f "$BUILD_DIR/user.img" ]]; then
    QEMU_OPTS+=(
        -drive "file=$BUILD_DIR/user.img,if=none,format=raw,id=disk1"
        -device virtio-blk-device,drive=disk1
    )
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
