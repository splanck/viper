#!/bin/bash
# ViperOS QEMU UEFI Boot Script
# Boots ViperOS via UEFI using VBoot bootloader
set -e

BUILD_DIR="${1:-build}"
MEMORY="${MEMORY:-128M}"
MODE="${MODE:-gui}"

# Find AAVMF/EDK2 firmware for AArch64
FIRMWARE=""
FIRMWARE_PATHS=(
    # macOS (Homebrew)
    "/opt/homebrew/share/qemu/edk2-aarch64-code.fd"
    "/usr/local/share/qemu/edk2-aarch64-code.fd"
    # Linux (various distros)
    "/usr/share/AAVMF/AAVMF_CODE.fd"
    "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
    "/usr/share/edk2/aarch64/QEMU_EFI.fd"
    "/usr/share/OVMF/AAVMF_CODE.fd"
    "/usr/share/qemu/edk2-aarch64-code.fd"
)

for path in "${FIRMWARE_PATHS[@]}"; do
    if [[ -f "$path" ]]; then
        FIRMWARE="$path"
        break
    fi
done

if [[ -z "$FIRMWARE" ]]; then
    echo "Error: UEFI firmware for AArch64 not found"
    echo "Searched paths:"
    for path in "${FIRMWARE_PATHS[@]}"; do
        echo "  - $path"
    done
    echo ""
    echo "On macOS: brew install qemu"
    echo "On Ubuntu/Debian: apt install qemu-efi-aarch64"
    exit 1
fi

echo "Using firmware: $FIRMWARE"

# Check if ESP image exists
if [[ ! -f "$BUILD_DIR/esp.img" ]]; then
    echo "Error: ESP image not found at $BUILD_DIR/esp.img"
    echo "Run 'scripts/make-esp.sh $BUILD_DIR' first"
    exit 1
fi

# Find QEMU
QEMU=""
QEMU_PATHS=(
    "/opt/homebrew/opt/qemu/bin/qemu-system-aarch64"
    "/usr/local/bin/qemu-system-aarch64"
    "/usr/bin/qemu-system-aarch64"
    "qemu-system-aarch64"
)

for path in "${QEMU_PATHS[@]}"; do
    if command -v "$path" &> /dev/null || [[ -x "$path" ]]; then
        QEMU="$path"
        break
    fi
done

if [[ -z "$QEMU" ]]; then
    echo "Error: qemu-system-aarch64 not found"
    exit 1
fi

echo "Using QEMU: $QEMU"

# Display options
DISPLAY_OPTS=""
SERIAL_OPTS="-serial mon:stdio"
case "$MODE" in
    gui)
        # Use default display (SDL on macOS, GTK on Linux)
        DISPLAY_OPTS=""
        ;;
    headless)
        DISPLAY_OPTS="-display none -vnc :0"
        ;;
    serial)
        # Pure serial mode - no graphics
        DISPLAY_OPTS="-nographic"
        SERIAL_OPTS=""  # -nographic already provides serial
        ;;
esac

# Disk options - ViperFS disk as second virtio-blk device
DISK_OPTS=""
if [[ -f "$BUILD_DIR/disk.img" ]]; then
    echo "Using disk image: $BUILD_DIR/disk.img"
    DISK_OPTS="-device virtio-blk-device,drive=viperfs -drive id=viperfs,file=$BUILD_DIR/disk.img,format=raw,if=none"
fi

# Network options
NET_OPTS="-netdev user,id=net0 -device virtio-net-device,netdev=net0"
echo "Network: virtio-net (user mode, 10.0.2.15)"

# RNG device
RNG_OPTS="-device virtio-rng-device"

# Input devices
INPUT_OPTS="-device virtio-keyboard-device -device virtio-mouse-device"

# Debug options
DEBUG_OPTS=""
if [[ "$2" == "--debug" ]] || [[ "$1" == "--debug" ]]; then
    DEBUG_OPTS="-s -S"
    echo "Waiting for GDB on localhost:1234..."
    echo "Connect with: gdb-multiarch -ex 'target remote :1234'"
fi

echo ""
echo "Starting QEMU (UEFI boot)..."
echo "  Memory: $MEMORY"
echo "  Mode: $MODE"
echo ""

exec "$QEMU" \
    -machine virt \
    -cpu cortex-a72 \
    -m "$MEMORY" \
    -drive if=pflash,format=raw,readonly=on,file="$FIRMWARE" \
    -drive file="$BUILD_DIR/esp.img",format=raw,if=virtio,media=disk \
    $DISK_OPTS \
    $NET_OPTS \
    $RNG_OPTS \
    $INPUT_OPTS \
    $SERIAL_OPTS \
    $DISPLAY_OPTS \
    $DEBUG_OPTS \
    -no-reboot
