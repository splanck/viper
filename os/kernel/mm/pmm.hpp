//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/mm/pmm.hpp
// Purpose: Physical Memory Manager interface (page allocation/freeing).
// Key invariants: Bitmap tracks page state; 0=free, 1=used.
// Ownership/Lifetime: Global singleton; initialized once by kernel_main.
// Links: kernel/mm/pmm.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

/**
 * @file pmm.hpp
 * @brief Physical Memory Manager (PMM) interface.
 *
 * @details
 * The PMM tracks allocation state of physical 4KiB pages and provides
 * page-granular allocation and freeing to kernel subsystems (page tables, heap
 * backing, DMA buffers, etc.).
 *
 * The current PMM implementation uses a bitmap with one bit per page:
 * - `0` means the page is free.
 * - `1` means the page is reserved or currently allocated.
 *
 * During initialization the bitmap is placed in RAM just after the kernel
 * image, all pages are pessimistically marked used, and then a pass marks
 * usable pages as free while keeping reserved regions allocated (kernel image,
 * bitmap storage, and other fixed reservations).
 */
namespace pmm
{

/** @brief Base page size in bytes (4 KiB). */
constexpr u64 PAGE_SIZE = 4096;
/** @brief log2(PAGE_SIZE), used for shifting addresses into page indices. */
constexpr u64 PAGE_SHIFT = 12;

/**
 * @brief Align an address up to the next page boundary.
 *
 * @param addr Address in bytes.
 * @return The smallest address `>= addr` that is aligned to @ref PAGE_SIZE.
 */
constexpr u64 page_align_up(u64 addr)
{
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/**
 * @brief Align an address down to the current page boundary.
 *
 * @param addr Address in bytes.
 * @return The largest address `<= addr` that is aligned to @ref PAGE_SIZE.
 */
constexpr u64 page_align_down(u64 addr)
{
    return addr & ~(PAGE_SIZE - 1);
}

/**
 * @brief Initialize the physical memory manager.
 *
 * @details
 * Sets the managed RAM range, places and initializes the allocation bitmap,
 * and marks pages as free or reserved. The caller supplies the physical end of
 * the kernel image so the PMM can avoid allocating pages that contain the
 * kernel itself or the PMM's own bookkeeping data.
 *
 * @param ram_start Physical address where RAM begins.
 * @param ram_size Total RAM size in bytes.
 * @param kernel_end Physical address immediately after the kernel image.
 */
void init(u64 ram_start, u64 ram_size, u64 kernel_end);

/**
 * @brief Allocate a single physical page.
 *
 * @details
 * Performs a first-fit scan of the bitmap to locate a free page, marks it
 * allocated, and returns the page's physical base address.
 *
 * @return Physical base address of the allocated page, or `0` on failure.
 */
u64 alloc_page();

/**
 * @brief Allocate a contiguous run of physical pages.
 *
 * @details
 * Searches for `count` consecutive free pages. If found, marks the entire run
 * allocated and returns the physical base address of the first page.
 *
 * @param count Number of pages to allocate.
 * @return Physical base address of the first page in the run, or `0` on failure.
 */
u64 alloc_pages(u64 count);

/**
 * @brief Free a single physical page.
 *
 * @details
 * Marks the page corresponding to `phys_addr` as free. The address must lie
 * within the managed RAM range. The implementation performs basic checks and
 * may emit warnings for invalid frees or double frees.
 *
 * @param phys_addr Physical base address of the page to free.
 */
void free_page(u64 phys_addr);

/**
 * @brief Free a contiguous run of physical pages.
 *
 * @details
 * Frees `count` pages starting at `phys_addr`. This is a convenience wrapper
 * around repeated calls to @ref free_page.
 *
 * @param phys_addr Physical base address of the first page.
 * @param count Number of pages to free.
 */
void free_pages(u64 phys_addr, u64 count);

/**
 * @brief Get the total number of pages managed by the PMM.
 *
 * @return Total page count.
 */
u64 get_total_pages();
/**
 * @brief Get the number of currently free pages.
 *
 * @return Free page count.
 */
u64 get_free_pages();
/**
 * @brief Get the number of currently used/reserved pages.
 *
 * @return Used page count.
 */
u64 get_used_pages();

/**
 * @brief Convert a physical address to a kernel virtual address.
 *
 * @details
 * The kernel currently uses an identity mapping (VA == PA) during bring-up.
 * This helper centralizes the assumption and provides a single place to update
 * once the kernel transitions to a different virtual memory layout.
 *
 * @param phys Physical address.
 * @return Virtual address corresponding to `phys`.
 */
inline void *phys_to_virt(u64 phys)
{
    return reinterpret_cast<void *>(phys);
}

/**
 * @brief Convert a kernel virtual address to a physical address.
 *
 * @details
 * Inverse of @ref phys_to_virt for the current identity-mapped model.
 *
 * @param virt Virtual address.
 * @return Physical address corresponding to `virt`.
 */
inline u64 virt_to_phys(void *virt)
{
    return reinterpret_cast<u64>(virt);
}

} // namespace pmm
