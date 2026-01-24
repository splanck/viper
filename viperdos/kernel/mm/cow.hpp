/**
 * @file cow.hpp
 * @brief Copy-on-Write (COW) page management.
 *
 * @details
 * Implements per-page reference counting to support efficient fork() via COW.
 * When multiple processes share a physical page (marked read-only), a write
 * fault triggers a copy of the page data to a new physical page owned solely
 * by the writing process.
 *
 * The CowManager tracks reference counts for all shared pages. When a page's
 * refcount drops to 1, the owning process can be given write access directly.
 * When refcount reaches 0, the page can be freed.
 */
#pragma once

#include "../include/types.hpp"
#include "../lib/spinlock.hpp"

namespace mm::cow {

/**
 * @brief Per-page metadata for COW tracking.
 */
struct PageInfo {
    u16 refcount; ///< Number of address spaces sharing this page
    u16 flags;    ///< Page state flags
};

/**
 * @brief Page state flags.
 */
namespace page_flags {
constexpr u16 COW = (1 << 0);    ///< Page is copy-on-write
constexpr u16 SHARED = (1 << 1); ///< Page is shared (don't COW)
} // namespace page_flags

/**
 * @brief VMA flags for COW tracking.
 */
namespace vma_flags {
constexpr u8 COW = (1 << 0);    ///< This VMA has COW pages
constexpr u8 SHARED = (1 << 1); ///< Shared mapping (not COW)
} // namespace vma_flags

/**
 * @brief COW manager for page reference counting.
 *
 * @details
 * Maintains per-page metadata in a flat array indexed by page frame number.
 * The array is allocated during init() and covers all physical pages in the
 * managed memory region.
 */
class CowManager {
  public:
    CowManager() = default;

    // Non-copyable
    CowManager(const CowManager &) = delete;
    CowManager &operator=(const CowManager &) = delete;

    /**
     * @brief Initialize the COW manager.
     *
     * @param mem_start Start of physical memory region.
     * @param mem_end End of physical memory region.
     * @param page_info_array Pre-allocated array for page info (or nullptr to allocate).
     * @return true on success, false on failure.
     */
    bool init(u64 mem_start, u64 mem_end, PageInfo *page_info_array = nullptr);

    /**
     * @brief Increment reference count for a page.
     *
     * @param phys_page Physical address of the page (page-aligned).
     */
    void inc_ref(u64 phys_page);

    /**
     * @brief Decrement reference count for a page.
     *
     * @param phys_page Physical address of the page.
     * @return true if the page should be freed (refcount reached 0).
     */
    bool dec_ref(u64 phys_page);

    /**
     * @brief Get the reference count for a page.
     *
     * @param phys_page Physical address of the page.
     * @return Current reference count.
     */
    u16 get_ref(u64 phys_page) const;

    /**
     * @brief Mark a page as copy-on-write.
     *
     * @param phys_page Physical address of the page.
     */
    void mark_cow(u64 phys_page);

    /**
     * @brief Clear the COW flag for a page.
     *
     * @param phys_page Physical address of the page.
     */
    void clear_cow(u64 phys_page);

    /**
     * @brief Check if a page is marked copy-on-write.
     *
     * @param phys_page Physical address of the page.
     * @return true if the page is COW.
     */
    bool is_cow(u64 phys_page) const;

    /**
     * @brief Check if manager is initialized.
     */
    bool is_initialized() const {
        return initialized_;
    }

    /**
     * @brief Get total pages managed.
     */
    u64 total_pages() const {
        return total_pages_;
    }

  private:
    PageInfo *page_info_{nullptr}; ///< Array of per-page metadata
    u64 mem_start_{0};             ///< Start of managed memory region
    u64 mem_end_{0};               ///< End of managed memory region
    u64 total_pages_{0};           ///< Total pages in managed region
    bool initialized_{false};
    mutable Spinlock lock_;

    /**
     * @brief Convert physical address to page index.
     */
    u64 phys_to_index(u64 phys) const {
        return (phys - mem_start_) >> 12; // PAGE_SHIFT = 12
    }

    /**
     * @brief Check if a physical address is within managed range.
     */
    bool is_valid_page(u64 phys) const {
        return phys >= mem_start_ && phys < mem_end_;
    }
};

/**
 * @brief Get the global COW manager instance.
 */
CowManager &cow_manager();

/**
 * @brief Result of COW fault handling.
 */
enum class CowResult {
    HANDLED,       ///< Page was copied or made writable
    ALREADY_OWNED, ///< Page already exclusively owned, just made writable
    OUT_OF_MEMORY, ///< Failed to allocate new page
    NOT_COW,       ///< Page is not a COW page
    ERROR,         ///< Other error
};

} // namespace mm::cow
