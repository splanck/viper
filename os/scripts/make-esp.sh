#!/bin/bash
# ViperOS ESP Image Creation Script (macOS/Linux)
# Creates a simple FAT32 disk image for UEFI booting
set -e

BUILD_DIR="${1:-build}"
ESP_SIZE_MB=64
ESP_IMG="$BUILD_DIR/esp.img"

echo "Creating UEFI ESP image: $ESP_IMG"

# Remove old image
rm -f "$ESP_IMG"

# Create empty image
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$ESP_SIZE_MB 2>/dev/null

# Create FAT32 filesystem (superfloppy - no partition table)
if command -v mkfs.vfat &> /dev/null; then
    mkfs.vfat -F 32 -n "VIPERESP" "$ESP_IMG"
elif command -v newfs_msdos &> /dev/null; then
    newfs_msdos -F 32 "$ESP_IMG"
else
    echo "Error: No FAT32 formatter found"
    exit 1
fi

# Add files using mtools
echo "Adding files to ESP..."

mmd -i "$ESP_IMG" ::EFI
mmd -i "$ESP_IMG" ::EFI/BOOT
mmd -i "$ESP_IMG" ::viperos

if [[ -f "$BUILD_DIR/BOOTAA64.EFI" ]]; then
    mcopy -i "$ESP_IMG" "$BUILD_DIR/BOOTAA64.EFI" ::EFI/BOOT/
    echo "  Copied BOOTAA64.EFI to EFI/BOOT/"
fi

if [[ -f "$BUILD_DIR/kernel.sys" ]]; then
    mcopy -i "$ESP_IMG" "$BUILD_DIR/kernel.sys" ::viperos/
    echo "  Copied kernel.sys to viperos/"
fi

echo "\\EFI\\BOOT\\BOOTAA64.EFI" > /tmp/startup.nsh
mcopy -i "$ESP_IMG" /tmp/startup.nsh ::
rm /tmp/startup.nsh
echo "  Created startup.nsh"

echo ""
echo "ESP image created: $ESP_IMG"
mdir -i "$ESP_IMG" ::EFI/BOOT/
