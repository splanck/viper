//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file vboot.hpp
 * @brief VBoot information structure for kernel.
 *
 * @details
 * This header defines the VBootInfo structure that is passed from the VBoot
 * UEFI bootloader to the kernel. When booting via UEFI, the kernel receives
 * a pointer to this structure in x0.
 *
 * When booting via QEMU -kernel, x0 contains a DTB pointer instead. The kernel
 * can distinguish between the two by checking the magic number.
 */

#pragma once

#include "types.hpp"

namespace viper::vboot {

/// Magic number to validate boot info: "VIPER\0" as uint64
constexpr u64 VBOOT_MAGIC = 0x564950455200ULL;

/// Maximum memory regions we track
constexpr int MAX_MEMORY_REGIONS = 64;

/// Memory region types
enum MemoryType : u32 {
    MEMORY_USABLE = 1,
    MEMORY_RESERVED = 2,
    MEMORY_ACPI = 3,
    MEMORY_MMIO = 4,
};

/// Framebuffer information from GOP
struct Framebuffer {
    u64 base;         ///< Physical address of framebuffer
    u32 width;        ///< Width in pixels
    u32 height;       ///< Height in pixels
    u32 pitch;        ///< Bytes per scanline
    u32 bpp;          ///< Bits per pixel (typically 32)
    u32 pixel_format; ///< 0 = BGR, 1 = RGB
    u32 reserved;
};

/// Memory region descriptor
struct MemoryRegion {
    u64 base; ///< Physical base address
    u64 size; ///< Size in bytes
    u32 type; ///< MemoryType
    u32 reserved;
};

/// Boot information structure passed from VBoot to kernel
struct Info {
    u64 magic;               ///< VBOOT_MAGIC
    u64 hhdm_base;           ///< Higher-half direct map base
    u64 kernel_phys_base;    ///< Kernel physical load address
    u64 kernel_virt_base;    ///< Kernel virtual address
    u64 kernel_size;         ///< Kernel size in bytes
    u64 ttbr0;               ///< TTBR0 value (identity map)
    u64 ttbr1;               ///< TTBR1 value (kernel map)
    Framebuffer framebuffer; ///< Framebuffer info
    u32 memory_region_count; ///< Number of memory regions
    u32 reserved;
    MemoryRegion memory_regions[MAX_MEMORY_REGIONS];
};

/**
 * @brief Check if a boot info pointer is valid VBootInfo.
 *
 * @param ptr Pointer to check.
 * @return true if ptr points to a valid VBootInfo structure.
 */
inline bool is_valid(const void *ptr) {
    if (!ptr)
        return false;
    const Info *info = static_cast<const Info *>(ptr);
    return info->magic == VBOOT_MAGIC;
}

} // namespace viper::vboot
