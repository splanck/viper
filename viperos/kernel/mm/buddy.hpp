//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/mm/buddy.hpp
// Purpose: Buddy allocator for contiguous physical page runs.
// Key invariants: Power-of-two blocks; buddies coalesce on free.
// Ownership/Lifetime: Global singleton; initialized after PMM.
// Links: kernel/mm/buddy.cpp
//
//===----------------------------------------------------------------------===//

/**
 * @file buddy.hpp
 * @brief Buddy allocator for physical memory management.
 *
 * @details
 * Implements a buddy allocator for O(log n) allocation and deallocation of
 * contiguous physical page runs. The allocator maintains free lists for each
 * power-of-two block size (order), from single pages up to 2MB blocks.
 *
 * Order 0 = 1 page (4KB)
 * Order 1 = 2 pages (8KB)
 * ...
 * Order 9 = 512 pages (2MB)
 *
 * @see https://en.wikipedia.org/wiki/Buddy_memory_allocation
 */

#pragma once

#include "../include/types.hpp"
#include "../lib/spinlock.hpp"

namespace mm::buddy
{

/** @brief Maximum order (2^MAX_ORDER pages = 2MB max block). */
constexpr u32 MAX_ORDER = 10;

/** @brief Page size in bytes. */
constexpr u64 PAGE_SIZE = 4096;

/** @brief Page shift (log2 of PAGE_SIZE). */
constexpr u32 PAGE_SHIFT = 12;

/**
 * @brief Free block header stored in the first 16 bytes of each free block.
 *
 * @details
 * When a block is free, we store metadata in the block itself. This is safe
 * because the block isn't being used for anything else.
 */
struct FreeBlock
{
    FreeBlock *next; ///< Next free block in this order's list
    u32 order;       ///< Block order (for verification)
    u32 _pad;
};

/**
 * @brief Per-order free list and statistics.
 */
struct FreeArea
{
    FreeBlock *free_list; ///< Head of free block list for this order
    u64 count;            ///< Number of free blocks at this order
};

/**
 * @brief Buddy allocator state.
 */
class BuddyAllocator
{
  public:
    BuddyAllocator() = default;

    // Non-copyable
    BuddyAllocator(const BuddyAllocator &) = delete;
    BuddyAllocator &operator=(const BuddyAllocator &) = delete;

    /**
     * @brief Initialize the buddy allocator.
     *
     * @param mem_start Start of managed memory region (page-aligned).
     * @param mem_end End of managed memory region (page-aligned).
     * @param reserved_end End of reserved area at start (kernel, etc).
     * @return true on success, false on error.
     */
    bool init(u64 mem_start, u64 mem_end, u64 reserved_end);

    /**
     * @brief Allocate pages of a given order.
     *
     * @param order Block order (0 = 1 page, 1 = 2 pages, ...).
     * @return Physical address of allocated block, or 0 on failure.
     */
    u64 alloc_pages(u32 order);

    /**
     * @brief Free pages of a given order.
     *
     * @param addr Physical address of block to free.
     * @param order Block order that was allocated.
     */
    void free_pages(u64 addr, u32 order);

    /**
     * @brief Allocate a single page (order 0).
     */
    u64 alloc_page()
    {
        return alloc_pages(0);
    }

    /**
     * @brief Free a single page.
     */
    void free_page(u64 addr)
    {
        free_pages(addr, 0);
    }

    /**
     * @brief Get total number of pages managed.
     */
    u64 total_pages() const
    {
        return total_pages_;
    }

    /**
     * @brief Get number of free pages.
     */
    u64 free_pages_count() const;

    /**
     * @brief Check if allocator is initialized.
     */
    bool is_initialized() const
    {
        return initialized_;
    }

    /**
     * @brief Dump allocator state to serial console.
     */
    void dump() const;

  private:
    FreeArea free_areas_[MAX_ORDER]; ///< Free lists for each order
    u64 mem_start_{0};               ///< Start of managed memory
    u64 mem_end_{0};                 ///< End of managed memory
    u64 total_pages_{0};             ///< Total pages in managed region
    bool initialized_{false};
    mutable Spinlock lock_;

    /**
     * @brief Convert physical address to page frame number.
     */
    u64 addr_to_pfn(u64 addr) const
    {
        return (addr - mem_start_) >> PAGE_SHIFT;
    }

    /**
     * @brief Convert page frame number to physical address.
     */
    u64 pfn_to_addr(u64 pfn) const
    {
        return mem_start_ + (pfn << PAGE_SHIFT);
    }

    /**
     * @brief Get buddy address for a block.
     */
    u64 get_buddy_addr(u64 addr, u32 order) const
    {
        u64 block_size = PAGE_SIZE << order;
        return addr ^ block_size;
    }

    /**
     * @brief Add a block to a free list.
     */
    void add_to_free_list(u64 addr, u32 order);

    /**
     * @brief Remove a specific block from a free list.
     */
    bool remove_from_free_list(u64 addr, u32 order);

    /**
     * @brief Pop the first block from a free list.
     */
    u64 pop_from_free_list(u32 order);

    /**
     * @brief Try to coalesce a block with its buddy.
     */
    void try_coalesce(u64 addr, u32 order);

    /**
     * @brief Split a block into two buddies.
     */
    void split_block(u32 order);
};

/**
 * @brief Get the global buddy allocator instance.
 */
BuddyAllocator &get_allocator();

/**
 * @brief Calculate order needed for a given page count.
 */
inline u32 pages_to_order(u64 pages)
{
    if (pages <= 1)
        return 0;

    u32 order = 0;
    u64 size = 1;
    while (size < pages && order < MAX_ORDER - 1)
    {
        order++;
        size <<= 1;
    }
    return order;
}

} // namespace mm::buddy
