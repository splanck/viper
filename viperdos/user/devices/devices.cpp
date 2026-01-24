/**
 * @file devices.cpp
 * @brief Hardware device listing utility for ViperDOS.
 *
 * @details
 * This utility lists all detected hardware devices in the system.
 * It uses the SYS_DEVICE_LIST syscall to query the kernel for device info.
 *
 * Usage:
 *   devices           - List all detected hardware
 */

#include "../syscall.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Device info structure (matches kernel definition)
struct DeviceInfo {
    char name[32];
    char type[16];
    u32 flags;
    u32 irq;
};

// Device flags
constexpr u32 DEVICE_FLAG_ACTIVE = (1 << 0);
constexpr u32 DEVICE_FLAG_VIRTUAL = (1 << 1);

// Device list syscall wrapper
static i32 get_device_list(DeviceInfo *devices, u32 max_count) {
    auto r =
        sys::syscall2(SYS_DEVICE_LIST, reinterpret_cast<u64>(devices), static_cast<u64>(max_count));
    if (r.ok())
        return static_cast<i32>(r.val0);
    return static_cast<i32>(r.error);
}

extern "C" void _start() {
    printf("\n=== ViperDOS Hardware Devices ===\n\n");

    DeviceInfo devices[16];
    memset(devices, 0, sizeof(devices));

    i32 count = get_device_list(devices, 16);

    if (count < 0) {
        printf("Error: Failed to get device list (error %d)\n", count);
        sys::exit(1);
    }

    if (count == 0) {
        printf("No devices detected.\n");
        sys::exit(0);
    }

    // Print header
    printf("%-20s %-12s %-8s %s\n", "Name", "Type", "IRQ", "Status");
    printf("%-20s %-12s %-8s %s\n", "--------------------", "------------", "--------", "------");

    // Print each device
    for (i32 i = 0; i < count; i++) {
        DeviceInfo *dev = &devices[i];

        // Name column (20 chars)
        printf("%-20s ", dev->name);

        // Type column (12 chars)
        printf("%-12s ", dev->type);

        // IRQ column (8 chars)
        if (dev->irq > 0)
            printf("%-8u ", dev->irq);
        else
            printf("%-8s ", "-");

        // Status
        if (dev->flags & DEVICE_FLAG_ACTIVE)
            printf("active");
        else
            printf("inactive");

        if (dev->flags & DEVICE_FLAG_VIRTUAL)
            printf(",virt");

        printf("\n");
    }

    printf("\n%d device%s detected.\n\n", count, count == 1 ? "" : "s");
    sys::exit(0);
}
