/**
 * @file vboot.h
 * @brief Boot information structures used by the ViperDOS UEFI bootloader.
 *
 * @details
 * The UEFI bootloader (`vboot`) is responsible for loading the kernel image,
 * collecting platform information (memory map, framebuffer details), and
 * transitioning from UEFI's execution environment into the kernel's AArch64
 * execution model.
 *
 * This header defines the data structures that `vboot` would pass to the kernel
 * at entry time. The goal is to provide the kernel with:
 * - A validated "boot info" block (via a magic value).
 * - The physical/virtual kernel load addresses and size.
 * - Page table roots used during the handoff.
 * - Framebuffer information for early graphics console.
 * - A simplified memory map describing usable and reserved regions.
 *
 * The current bootloader implementation may not yet populate all fields, but
 * the structures define the intended ABI contract between bootloader and
 * kernel.
 */

#ifndef VBOOT_VBOOT_H
#define VBOOT_VBOOT_H

#include <stdint.h>

/** @brief Magic value used to validate a @ref VBootInfo block (`"VIPER\\0"`). */
#define VBOOT_MAGIC 0x564950455200ULL

/** @brief Maximum number of memory regions stored in @ref VBootInfo. */
#define VBOOT_MAX_MEMORY_REGIONS 64

/** @name Memory region type identifiers
 *  @brief Values stored in @ref VBootMemoryRegion::type.
 *  @{
 */
#define VBOOT_MEMORY_USABLE 1   /**< Usable RAM that can be managed by the PMM. */
#define VBOOT_MEMORY_RESERVED 2 /**< Reserved region (firmware, bootloader, etc.). */
#define VBOOT_MEMORY_ACPI 3     /**< ACPI tables / reclaimable firmware data. */
#define VBOOT_MEMORY_MMIO 4     /**< Memory-mapped I/O region (device registers). */

/** @} */

/**
 * @brief Framebuffer description provided by UEFI graphics output protocol.
 *
 * @details
 * The bootloader queries UEFI's GOP (Graphics Output Protocol) to obtain a
 * linear framebuffer and mode details. The kernel can use this information to
 * draw an early console before higher-level graphics drivers are available.
 */
typedef struct {
    uint64_t base;         /**< Physical address of the framebuffer base. */
    uint32_t width;        /**< Width in pixels. */
    uint32_t height;       /**< Height in pixels. */
    uint32_t pitch;        /**< Bytes per scanline (stride). */
    uint32_t bpp;          /**< Bits per pixel (commonly 32). */
    uint32_t pixel_format; /**< Pixel format (bootloader-defined encoding). */
    uint32_t reserved;
} VBootFramebuffer;

/**
 * @brief One simplified memory map entry.
 *
 * @details
 * UEFI provides a detailed memory map with many types. The bootloader may
 * collapse those types into a smaller set relevant to the kernel:
 * - Usable RAM regions can be fed into the physical memory manager.
 * - Reserved/MMIO regions must be excluded from allocation.
 */
typedef struct {
    uint64_t base; /**< Physical base address of the region. */
    uint64_t size; /**< Size of the region in bytes. */
    uint32_t type; /**< Region type (`VBOOT_MEMORY_*`). */
    uint32_t reserved;
} VBootMemoryRegion;

/**
 * @brief Boot information block passed from `vboot` to the kernel.
 *
 * @details
 * The bootloader passes a pointer to this structure to the kernel entry point.
 * The kernel should validate the block by checking @ref magic before trusting
 * other fields.
 *
 * Fields:
 * - `hhdm_base`: base of the higher-half direct map (if used).
 * - `kernel_phys_base` / `kernel_virt_base`: where the kernel image was loaded.
 * - `kernel_size`: size of the loaded kernel image in bytes.
 * - `ttbr0` / `ttbr1`: translation table base registers used during handoff.
 * - `framebuffer`: GOP framebuffer details for early graphics.
 * - `memory_regions`: simplified memory map.
 */
typedef struct {
    uint64_t magic;               /**< Must equal @ref VBOOT_MAGIC. */
    uint64_t hhdm_base;           /**< Higher-half direct map base address. */
    uint64_t kernel_phys_base;    /**< Kernel physical load address. */
    uint64_t kernel_virt_base;    /**< Kernel virtual base address. */
    uint64_t kernel_size;         /**< Kernel image size in bytes. */
    uint64_t ttbr0;               /**< TTBR0 value used during transition (identity map). */
    uint64_t ttbr1;               /**< TTBR1 value used during transition (kernel map). */
    VBootFramebuffer framebuffer; /**< Framebuffer information. */
    uint32_t memory_region_count; /**< Number of valid entries in @ref memory_regions. */
    uint32_t reserved;
    VBootMemoryRegion memory_regions[VBOOT_MAX_MEMORY_REGIONS];
} VBootInfo;

#endif // VBOOT_VBOOT_H
