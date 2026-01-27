//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file bootinfo.hpp
 * @brief Unified boot information abstraction.
 *
 * @details
 * This module provides a single, kernel-internal API for accessing the system
 * information passed in from the boot environment. It is designed to hide the
 * differences between early bring-up boot paths so the rest of the kernel can
 * consume one consistent @ref boot::Info structure.
 *
 * Supported boot environments:
 * - **VBoot (UEFI)**: the kernel is started by the `vboot` UEFI loader and x0
 *   points to a `VBootInfo` structure populated from the UEFI memory map and
 *   GOP framebuffer.
 * - **QEMU direct (-kernel)**: QEMU loads the kernel directly and x0 points to
 *   a device tree blob (DTB). For bring-up, this module uses conservative
 *   hardcoded QEMU `virt` defaults when DTB parsing is not available.
 *
 * The bootinfo module is initialized early in `kernel_main` and provides:
 * - Framebuffer information (UEFI GOP framebuffer or empty info when absent).
 * - Memory region information (UEFI memory map or hardcoded defaults).
 * - Boot method detection so platform-specific paths can be selected.
 */

#pragma once

#include "../include/types.hpp"

namespace boot {

/**
 * @brief Maximum number of memory regions stored in @ref Info.
 *
 * @details
 * UEFI firmware can return a large memory map. The bootloader (and this module)
 * collapses and truncates the list into a manageable fixed-size array for early
 * kernel bring-up.
 */
constexpr int MAX_MEMORY_REGIONS = 64;

/**
 * @brief Ways the kernel may have been started.
 *
 * @details
 * The boot method influences what information is available (e.g., whether
 * there is a UEFI framebuffer) and how reliable the memory map is (UEFI memory
 * map vs hardcoded defaults).
 */
enum class Method {
    Unknown,    ///< Could not determine boot method
    QemuDirect, ///< Booted via QEMU -kernel (DTB in x0)
    VBoot,      ///< Booted via VBoot UEFI loader (VBootInfo in x0)
};

/**
 * @brief Simplified memory region type identifiers.
 *
 * @details
 * Values mirror the bootloader's view of the UEFI memory map and are used by
 * early kernel memory initialization (PMM) to decide which regions are usable.
 */
enum class MemoryType : u32 {
    Usable = 1,   ///< Available for general use
    Reserved = 2, ///< Reserved by firmware
    Acpi = 3,     ///< ACPI tables/data
    Mmio = 4,     ///< Memory-mapped I/O
};

/**
 * @brief Framebuffer pixel format.
 *
 * @details
 * Encodes the byte order of 32-bit pixels as reported by UEFI GOP. This is
 * used by the early graphics console to interpret framebuffer memory.
 */
enum class PixelFormat : u32 {
    BGR = 0, ///< Blue-Green-Red (typical for UEFI GOP)
    RGB = 1, ///< Red-Green-Blue
};

/**
 * @brief Framebuffer information for early console output.
 *
 * @details
 * When booted via UEFI, `vboot` can provide the physical address and geometry
 * of a linear framebuffer allocated/configured by GOP. When booted directly by
 * QEMU, the framebuffer may be configured later using a RAM framebuffer device
 * (ramfb), in which case this structure may be empty during early boot.
 */
struct Framebuffer {
    u64 base;           ///< Physical address of framebuffer
    u32 width;          ///< Width in pixels
    u32 height;         ///< Height in pixels
    u32 pitch;          ///< Bytes per scanline
    u32 bpp;            ///< Bits per pixel (typically 32)
    PixelFormat format; ///< Pixel format (BGR or RGB)

    /**
     * @brief Return whether this framebuffer description looks usable.
     *
     * @details
     * A non-zero base plus non-zero width/height is treated as "valid". This
     * does not guarantee the memory is mapped yet; it only indicates that the
     * bootloader supplied plausible values.
     */
    bool is_valid() const {
        return base != 0 && width > 0 && height > 0;
    }
};

/**
 * @brief One physical memory region described by the boot environment.
 *
 * @details
 * Regions are described in physical address space. Early memory initialization
 * uses these ranges to seed the physical page allocator.
 */
struct MemoryRegion {
    u64 base;        ///< Physical base address
    u64 size;        ///< Size in bytes
    MemoryType type; ///< Region type
};

/**
 * @brief Unified boot information snapshot.
 *
 * @details
 * This structure is produced once during early boot and then treated as
 * read-only. It contains the information the kernel needs to bootstrap memory
 * management and early I/O.
 */
struct Info {
    Method method; ///< How kernel was booted

    // Framebuffer (from GOP or ramfb)
    Framebuffer framebuffer;

    // Memory regions (from UEFI or hardcoded)
    u32 memory_region_count;
    MemoryRegion memory_regions[MAX_MEMORY_REGIONS];

    // Kernel info (from VBoot or derived)
    u64 kernel_phys_base;
    u64 kernel_size;

    // DTB pointer (if QEMU direct boot)
    void *dtb;
};

/**
 * @brief Initialize boot info from the boot environment.
 *
 * @details
 * Parses the `boot_info` pointer passed in register x0 at kernel entry time.
 * The method is selected by validation:
 * - If the pointer refers to a valid `VBootInfo` block (magic check), the
 *   module extracts UEFI-provided framebuffer and memory map information.
 * - Otherwise, the pointer is treated as a DTB pointer and the module falls
 *   back to QEMU `virt` defaults when DTB parsing is not available.
 *
 * This function must be called once, early in `kernel_main`, before consumers
 * call any other `boot::` APIs.
 *
 * @param boot_info Pointer passed in x0 (VBootInfo or DTB)
 */
void init(void *boot_info);

/**
 * @brief Get the parsed boot information.
 *
 * @return Reference to the boot info structure.
 */
const Info &get_info();

/**
 * @brief Get the boot method.
 *
 * @return How the kernel was booted.
 */
Method get_method();

/**
 * @brief Get framebuffer information.
 *
 * @details
 * For VBoot: Returns GOP framebuffer info.
 * For QEMU direct: Returns empty info (caller should use ramfb).
 *
 * @return Reference to framebuffer info.
 */
const Framebuffer &get_framebuffer();

/**
 * @brief Check if we have a UEFI-provided framebuffer.
 *
 * @return true if VBoot provided GOP framebuffer info.
 */
bool has_uefi_framebuffer();

/**
 * @brief Get the number of memory regions.
 *
 * @return Number of valid memory regions.
 */
u32 get_memory_region_count();

/**
 * @brief Get a memory region by index.
 *
 * @param index Region index (0 to region_count-1).
 * @return Pointer to region, or nullptr if index out of bounds.
 */
const MemoryRegion *get_memory_region(u32 index);

/**
 * @brief Calculate total usable memory from memory regions.
 *
 * @return Total bytes of usable memory.
 */
u64 get_total_usable_memory();

/**
 * @brief Find RAM region containing kernel.
 *
 * @details
 * For PMM initialization, we need to know the RAM base and size.
 * This finds the largest usable memory region.
 *
 * @param out_base Output: RAM base address.
 * @param out_size Output: RAM size in bytes.
 * @return true if a usable region was found.
 */
bool get_ram_region(u64 &out_base, u64 &out_size);

/**
 * @brief Print the parsed boot info to the serial console.
 *
 * @details
 * Intended for bring-up diagnostics. The output includes:
 * - Boot method.
 * - Kernel physical base and size.
 * - Framebuffer geometry (if available).
 * - Enumerated memory regions and total usable memory.
 */
void dump();

} // namespace boot
