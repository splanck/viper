#!/bin/bash
# ViperOS Direct Kernel Boot Script
# Uses QEMU -kernel instead of UEFI boot
set -e

BUILD_DIR="${1:-build}"
MEMORY="${MEMORY:-128M}"
MODE="${MODE:-serial}"
DISK="${DISK:-$BUILD_DIR/disk.img}"

# Find kernel
KERNEL="$BUILD_DIR/kernel.elf"
if [[ ! -f "$KERNEL" ]]; then
    echo "Error: Kernel not found at $KERNEL"
    echo "Run 'cmake --build build' first"
    exit 1
fi

echo "Kernel: $KERNEL"

# Display and device options
DISPLAY_OPTS=""
DEVICE_OPTS=""

case "$MODE" in
    serial)
        # Serial only (no graphics)
        DISPLAY_OPTS="-nographic"
        ;;
    graphics)
        # Enable ramfb for simple framebuffer + virtio-input for keyboard/mouse
        # Use mux=on,signal=off to prevent stdin conflicts with virtio-input
        DEVICE_OPTS="-device ramfb -device virtio-keyboard-device -device virtio-mouse-device"
        DISPLAY_OPTS="-chardev stdio,id=ser0,mux=on,signal=off -serial chardev:ser0"
        ;;
    both)
        # Graphics with serial console
        DEVICE_OPTS="-device ramfb -device virtio-keyboard-device -device virtio-mouse-device"
        DISPLAY_OPTS="-chardev stdio,id=ser0,mux=on,signal=off -serial chardev:ser0"
        ;;
esac

# Disk options
DISK_OPTS=""
if [[ -f "$DISK" ]]; then
    echo "Disk: $DISK"
    DISK_OPTS="-global virtio-mmio.force-legacy=false -drive file=$DISK,if=none,format=raw,id=disk0 -device virtio-blk-device,drive=disk0"
else
    echo "No disk image found at $DISK (virtio-blk disabled)"
fi

# Network options (Phase 6)
NET_OPTS=""
if [[ "${NET:-yes}" == "yes" ]]; then
    NET_OPTS="-netdev user,id=net0 -device virtio-net-device,netdev=net0"
    # Enable packet capture for debugging if NET_DUMP is set
    if [[ -n "$NET_DUMP" ]]; then
        NET_OPTS="$NET_OPTS -object filter-dump,id=dump0,netdev=net0,file=$NET_DUMP"
    fi
    echo "Network: virtio-net (user mode, 10.0.2.15)"
fi

# Debug options
DEBUG_OPTS=""
if [[ "$2" == "--debug" ]]; then
    DEBUG_OPTS="-s -S"
    echo "Waiting for GDB on localhost:1234..."
fi

echo "Starting QEMU (mode: $MODE)..."
exec qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a72 \
    -m "$MEMORY" \
    -kernel "$KERNEL" \
    $DEVICE_OPTS \
    $DISK_OPTS \
    $NET_OPTS \
    $DISPLAY_OPTS \
    $DEBUG_OPTS \
    -no-reboot
