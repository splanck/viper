/**
 * @file cow.cpp
 * @brief Copy-on-Write page management implementation.
 *
 * @details
 * Implements per-page reference counting for COW support. The page info array
 * is stored in physical memory just after the PMM bitmap.
 */

#include "cow.hpp"
#include "../console/serial.hpp"
#include "pmm.hpp"

namespace mm::cow {

namespace {
// Global COW manager instance
CowManager g_cow_manager;
} // namespace

CowManager &cow_manager() {
    return g_cow_manager;
}

bool CowManager::init(u64 mem_start, u64 mem_end, PageInfo *page_info_array) {
    SpinlockGuard guard(lock_);

    if (initialized_) {
        serial::puts("[cow] Already initialized\n");
        return false;
    }

    // Align boundaries to page size
    constexpr u64 PAGE_SIZE = 4096;
    mem_start_ = (mem_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    mem_end_ = mem_end & ~(PAGE_SIZE - 1);

    if (mem_end_ <= mem_start_) {
        serial::puts("[cow] Invalid memory range\n");
        return false;
    }

    total_pages_ = (mem_end_ - mem_start_) >> 12;

    serial::puts("[cow] Initializing COW manager: ");
    serial::put_hex(mem_start_);
    serial::puts(" - ");
    serial::put_hex(mem_end_);
    serial::puts(" (");
    serial::put_dec(total_pages_);
    serial::puts(" pages)\n");

    // Calculate required size for page info array
    u64 info_size = total_pages_ * sizeof(PageInfo);
    u64 info_pages = (info_size + PAGE_SIZE - 1) / PAGE_SIZE;

    serial::puts("[cow] Page info array: ");
    serial::put_dec(info_size);
    serial::puts(" bytes (");
    serial::put_dec(info_pages);
    serial::puts(" pages)\n");

    if (page_info_array != nullptr) {
        // Use provided array
        page_info_ = page_info_array;
    } else {
        // Allocate page info array from PMM
        u64 info_phys = pmm::alloc_pages(info_pages);
        if (info_phys == 0) {
            serial::puts("[cow] Failed to allocate page info array\n");
            return false;
        }

        page_info_ = reinterpret_cast<PageInfo *>(pmm::phys_to_virt(info_phys));
        serial::puts("[cow] Page info array at ");
        serial::put_hex(info_phys);
        serial::puts("\n");
    }

    // Initialize all page info to zero
    for (u64 i = 0; i < total_pages_; i++) {
        page_info_[i].refcount_and_flags = 0;
    }

    initialized_ = true;
    serial::puts("[cow] COW manager initialized\n");

    return true;
}

void CowManager::inc_ref(u64 phys_page) {
    if (!initialized_)
        return;

    // Align to page boundary
    phys_page &= ~0xFFFULL;

    if (!is_valid_page(phys_page))
        return;

    u64 idx = phys_to_index(phys_page);

    // Atomic increment using compare-and-swap loop
    u32 old_val, new_val;
    do {
        old_val = page_info_[idx].refcount_and_flags;
        u16 refcount = static_cast<u16>(old_val & 0xFFFF);
        if (refcount >= 0xFFFF) return; // Saturated
        new_val = (old_val & 0xFFFF0000) | ((refcount + 1) & 0xFFFF);
    } while (!__sync_bool_compare_and_swap(&page_info_[idx].refcount_and_flags, old_val, new_val));
}

bool CowManager::dec_ref(u64 phys_page) {
    if (!initialized_)
        return false;

    // Align to page boundary
    phys_page &= ~0xFFFULL;

    if (!is_valid_page(phys_page))
        return false;

    u64 idx = phys_to_index(phys_page);

    // Atomic decrement using compare-and-swap loop
    u32 old_val, new_val;
    do {
        old_val = page_info_[idx].refcount_and_flags;
        u16 refcount = static_cast<u16>(old_val & 0xFFFF);
        if (refcount == 0) return false;
        new_val = (old_val & 0xFFFF0000) | ((refcount - 1) & 0xFFFF);
    } while (!__sync_bool_compare_and_swap(&page_info_[idx].refcount_and_flags, old_val, new_val));

    return (new_val & 0xFFFF) == 0;
}

u16 CowManager::get_ref(u64 phys_page) const {
    if (!initialized_)
        return 0;

    // Align to page boundary
    phys_page &= ~0xFFFULL;

    if (!is_valid_page(phys_page))
        return 0;

    u64 idx = phys_to_index(phys_page);
    // Atomic read - no lock needed
    return static_cast<u16>(page_info_[idx].refcount_and_flags & 0xFFFF);
}

void CowManager::mark_cow(u64 phys_page) {
    if (!initialized_)
        return;

    phys_page &= ~0xFFFULL;

    if (!is_valid_page(phys_page))
        return;

    u64 idx = phys_to_index(phys_page);

    // Atomic set COW flag using compare-and-swap
    u32 old_val, new_val;
    do {
        old_val = page_info_[idx].refcount_and_flags;
        new_val = old_val | (static_cast<u32>(page_flags::COW) << 16);
    } while (!__sync_bool_compare_and_swap(&page_info_[idx].refcount_and_flags, old_val, new_val));
}

void CowManager::clear_cow(u64 phys_page) {
    if (!initialized_)
        return;

    phys_page &= ~0xFFFULL;

    if (!is_valid_page(phys_page))
        return;

    u64 idx = phys_to_index(phys_page);

    // Atomic clear COW flag using compare-and-swap
    u32 old_val, new_val;
    do {
        old_val = page_info_[idx].refcount_and_flags;
        new_val = old_val & ~(static_cast<u32>(page_flags::COW) << 16);
    } while (!__sync_bool_compare_and_swap(&page_info_[idx].refcount_and_flags, old_val, new_val));
}

bool CowManager::is_cow(u64 phys_page) const {
    if (!initialized_)
        return false;

    phys_page &= ~0xFFFULL;

    if (!is_valid_page(phys_page))
        return false;

    u64 idx = phys_to_index(phys_page);
    // Atomic read - no lock needed
    u32 flags = (page_info_[idx].refcount_and_flags >> 16) & 0xFFFF;
    return (flags & page_flags::COW) != 0;
}

} // namespace mm::cow
