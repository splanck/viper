#pragma once

#include "../include/types.hpp"

/**
 * @file vmm.hpp
 * @brief Virtual Memory Manager (VMM) interface for AArch64.
 *
 * @details
 * The VMM provides primitives for manipulating AArch64 translation tables:
 * mapping virtual pages to physical pages, unmapping, performing address
 * translation, and issuing the TLB invalidations required after updates.
 *
 * The constants in this header define commonly used page descriptor bits and
 * presets appropriate for kernel mappings. The current kernel bring-up uses an
 * identity mapping, but this API is designed to evolve into a full MMU setup.
 */
namespace vmm
{

// Page table entry flags for AArch64
/**
 * @brief AArch64 translation table descriptor bit definitions.
 *
 * @details
 * These values match the AArch64 Long Descriptor format for 4KiB granule page
 * tables. They are composed into `PAGE_*` presets for common mappings.
 */
namespace pte
{
constexpr u64 VALID = 1ULL << 0; // Entry is valid
constexpr u64 TABLE = 1ULL << 1; // Points to next-level table (for L0-L2)
constexpr u64 BLOCK = 0ULL << 1; // Block descriptor (for L1-L2)
constexpr u64 PAGE = 1ULL << 1;  // Page descriptor (for L3)

// Access flag
constexpr u64 AF = 1ULL << 10; // Access flag (must be set)

// Shareability
constexpr u64 SH_NONE = 0ULL << 8;
constexpr u64 SH_OUTER = 2ULL << 8;
constexpr u64 SH_INNER = 3ULL << 8;

// Access permissions
constexpr u64 AP_RW_EL1 = 0ULL << 6; // EL1 read/write
constexpr u64 AP_RW_ALL = 1ULL << 6; // EL1/EL0 read/write
constexpr u64 AP_RO_EL1 = 2ULL << 6; // EL1 read-only
constexpr u64 AP_RO_ALL = 3ULL << 6; // EL1/EL0 read-only

// Execute-never
constexpr u64 UXN = 1ULL << 54; // Unprivileged execute-never
constexpr u64 PXN = 1ULL << 53; // Privileged execute-never

// Memory attribute index (MAIR)
constexpr u64 ATTR(u64 idx)
{
    return idx << 2;
}

// Common attribute indices (must match MAIR setup)
constexpr u64 ATTR_DEVICE = 0; // Device memory
constexpr u64 ATTR_NORMAL = 1; // Normal cacheable memory
} // namespace pte

// Common page flags
/** @brief Kernel read/write mapping for normal cacheable memory. */
constexpr u64 PAGE_KERNEL_RW = pte::VALID | pte::PAGE | pte::AF | pte::SH_INNER | pte::AP_RW_EL1 |
                               pte::UXN | pte::ATTR(pte::ATTR_NORMAL);
/** @brief Kernel read/execute mapping for normal cacheable memory. */
constexpr u64 PAGE_KERNEL_RX =
    pte::VALID | pte::PAGE | pte::AF | pte::SH_INNER | pte::AP_RO_EL1 | pte::ATTR(pte::ATTR_NORMAL);
/** @brief Kernel read-only, non-executable mapping for normal cacheable memory. */
constexpr u64 PAGE_KERNEL_RO = pte::VALID | pte::PAGE | pte::AF | pte::SH_INNER | pte::AP_RO_EL1 |
                               pte::UXN | pte::PXN | pte::ATTR(pte::ATTR_NORMAL);
/** @brief Device-memory mapping for MMIO registers. */
constexpr u64 PAGE_DEVICE = pte::VALID | pte::PAGE | pte::AF | pte::SH_NONE | pte::AP_RW_EL1 |
                            pte::UXN | pte::PXN | pte::ATTR(pte::ATTR_DEVICE);

/**
 * @brief Initialize the virtual memory manager.
 *
 * @details
 * Allocates and initializes a root translation table and prepares internal VMM
 * state. Depending on the current bring-up stage, the CPU may still be running
 * under a boot-time identity mapping; this routine prepares the infrastructure
 * for kernel-owned page tables.
 */
void init();

/**
 * @brief Map a single 4KiB page.
 *
 * @details
 * Walks the translation tables for `virt`, allocating intermediate tables as
 * needed, and installs a final-level page descriptor mapping to `phys` with
 * the supplied flags. A per-page TLB invalidation is performed afterwards.
 *
 * @param virt Virtual address (should be page-aligned).
 * @param phys Physical address (should be page-aligned).
 * @param flags Page descriptor flags.
 * @return `true` on success, `false` on allocation failure.
 */
bool map_page(u64 virt, u64 phys, u64 flags);

/**
 * @brief Map a range of bytes using page mappings.
 *
 * @details
 * Maps `size` bytes starting at `virt` to `phys` with identical flags for each
 * page. The size is rounded up to whole pages.
 *
 * @param virt Starting virtual address.
 * @param phys Starting physical address.
 * @param size Size in bytes.
 * @param flags Page descriptor flags.
 * @return `true` if the entire range is mapped, otherwise `false`.
 */
bool map_range(u64 virt, u64 phys, u64 size, u64 flags);

/**
 * @brief Unmap a single 4KiB page.
 *
 * @details
 * Clears the final-level descriptor for `virt` and invalidates the
 * corresponding TLB entry. Intermediate tables are not freed.
 *
 * @param virt Virtual address to unmap (page-aligned).
 */
void unmap_page(u64 virt);

/**
 * @brief Translate a virtual address to a physical address.
 *
 * @details
 * Walks the page tables to resolve `virt`. Returns `0` if the address is not
 * mapped. When block descriptors are present, the appropriate page offset is
 * applied to produce the final physical address.
 *
 * @param virt Virtual address to translate.
 * @return Physical address, or `0` if unmapped.
 */
u64 virt_to_phys(u64 virt);

/**
 * @brief Invalidate the TLB entry for a specific virtual page.
 *
 * @details
 * After updating a page mapping, the CPU may continue to use a cached
 * translation in the TLB. This routine invalidates the entry and issues the
 * barrier sequence required for correctness.
 *
 * @param virt Virtual address whose translation should be invalidated.
 */
void invalidate_page(u64 virt);

/**
 * @brief Invalidate the entire EL1 TLB.
 *
 * @details
 * Heavy-weight invalidation used when the kernel performs broad translation
 * table changes.
 */
void invalidate_all();

} // namespace vmm
