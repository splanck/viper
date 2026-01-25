#include "pmm.hpp"
#include "../console/serial.hpp"
#include "../include/constants.hpp"
#include "../lib/spinlock.hpp"
#include "buddy.hpp"

/**
 * @file pmm.cpp
 * @brief Bitmap-backed physical page allocator implementation.
 *
 * @details
 * This implementation manages a single contiguous RAM window and uses a bitmap
 * stored in RAM to track free vs used pages. It is designed to be simple and
 * transparent during early kernel bring-up:
 *
 * - Initialization places the bitmap just after the kernel image.
 * - All pages are initially marked used; a second pass marks usable RAM as free.
 * - Allocation uses first-fit scanning over bitmap words and bits.
 *
 * Diagnostic messages are printed via the serial console to aid bring-up.
 */
namespace pmm {

namespace {
// Spinlock protecting all PMM state
Spinlock pmm_lock;

// Whether buddy allocator is available
bool buddy_available = false;

// Memory region info
u64 mem_start = 0;
u64 mem_end = 0;
u64 total_pages = 0;
u64 free_count = 0; // Bitmap allocator free count

// Region boundaries for determining which allocator owns a page
u64 buddy_region_start = 0; // fb_end
u64 buddy_region_end = 0;   // mem_end

// Bitmap for tracking page allocation (pre-framebuffer region)
// Each bit represents one page: 0 = free, 1 = used
u64 *bitmap = nullptr;
u64 bitmap_size = 0; // Size in u64 words

// Next-free hint: last word index where we found a free page
// This avoids O(n) bitmap scans by starting from a likely-free location
u64 next_free_hint = 0;

// Helper to set a bit (mark page as used)
/**
 * @brief Mark a page as used in the allocation bitmap.
 *
 * @param page_idx Zero-based page index within the managed RAM range.
 */
inline void set_bit(u64 page_idx) {
    u64 word = page_idx / 64;
    u64 bit = page_idx % 64;
    bitmap[word] |= (1ULL << bit);
}

// Helper to clear a bit (mark page as free)
/**
 * @brief Mark a page as free in the allocation bitmap.
 *
 * @param page_idx Zero-based page index within the managed RAM range.
 */
inline void clear_bit(u64 page_idx) {
    u64 word = page_idx / 64;
    u64 bit = page_idx % 64;
    bitmap[word] &= ~(1ULL << bit);
}

// Helper to test a bit
/**
 * @brief Test whether a page is marked used.
 *
 * @param page_idx Zero-based page index within the managed RAM range.
 * @return `true` if the page is used/reserved, `false` if free.
 */
inline bool test_bit(u64 page_idx) {
    u64 word = page_idx / 64;
    u64 bit = page_idx % 64;
    return (bitmap[word] & (1ULL << bit)) != 0;
}

// Convert physical address to page index
/**
 * @brief Convert a physical address into a PMM page index.
 *
 * @param addr Physical address in the managed RAM window.
 * @return Zero-based page index.
 */
inline u64 addr_to_page(u64 addr) {
    return (addr - mem_start) >> PAGE_SHIFT;
}

// Convert page index to physical address
/**
 * @brief Convert a PMM page index into a physical base address.
 *
 * @param page_idx Zero-based page index.
 * @return Physical base address of the page.
 */
inline u64 page_to_addr(u64 page_idx) {
    return mem_start + (page_idx << PAGE_SHIFT);
}

/**
 * @brief Internal free_page without locking (caller must hold pmm_lock).
 */
void free_page_unlocked(u64 phys_addr) {
    if (phys_addr < mem_start || phys_addr >= mem_end) {
        serial::puts("[pmm] WARNING: Freeing invalid address ");
        serial::put_hex(phys_addr);
        serial::puts("\n");
        return;
    }

    u64 page = addr_to_page(phys_addr);
    if (!test_bit(page)) {
        serial::puts("[pmm] WARNING: Double-free at ");
        serial::put_hex(phys_addr);
        serial::puts("\n");
        return;
    }

    clear_bit(page);
    free_count++;
}

/**
 * @brief Initialize buddy allocator for post-framebuffer memory region.
 */
void init_buddy_region(u64 fb_end) {
    if (fb_end >= mem_end) {
        serial::puts("[pmm] fb_end >= mem_end, skipping buddy\n");
        return;
    }

    if (mm::buddy::get_allocator().init(fb_end, mem_end, fb_end)) {
        buddy_available = true;
        buddy_region_start = fb_end;
        buddy_region_end = mem_end;
        serial::puts("[pmm] Buddy region: ");
        serial::put_hex(fb_end);
        serial::puts(" - ");
        serial::put_hex(mem_end);
        serial::puts(" (");
        serial::put_dec((mem_end - fb_end) / (1024 * 1024));
        serial::puts(" MB, ");
        serial::put_dec(mm::buddy::get_allocator().free_pages_count());
        serial::puts(" pages)\n");
    } else {
        serial::puts("[pmm] Buddy allocator init failed\n");
    }
}

/**
 * @brief Initialize bitmap allocator for pre-framebuffer memory region.
 */
void init_bitmap_region(u64 bitmap_addr, u64 usable_start, u64 fb_start) {
    bitmap = reinterpret_cast<u64 *>(bitmap_addr);

    for (u64 i = 0; i < bitmap_size; i++)
        bitmap[i] = ~0ULL;
    free_count = 0;

    for (u64 addr = usable_start; addr < fb_start; addr += PAGE_SIZE) {
        u64 page = addr_to_page(addr);
        if (page < total_pages) {
            clear_bit(page);
            free_count++;
        }
    }

    serial::puts("[pmm] Bitmap: ");
    serial::put_dec(free_count);
    serial::puts(" pages (");
    serial::put_dec((free_count * PAGE_SIZE) / 1024);
    serial::puts(" KB)\n");
}

/**
 * @brief Print PMM summary statistics.
 */
void print_summary() {
    serial::puts("[pmm] Total: ");
    serial::put_dec(get_free_pages());
    serial::puts(" free / ");
    serial::put_dec(get_total_pages());
    serial::puts(" total pages (");
    serial::put_dec((get_free_pages() * PAGE_SIZE) / (1024 * 1024));
    serial::puts(" MB free)\n");
}

} // namespace

/** @copydoc pmm::init */
void init(u64 ram_start, u64 ram_size, u64 kernel_end, u64 fb_base, u64 fb_size_param) {
    serial::puts("[pmm] Initializing physical memory manager\n");

    mem_start = ram_start;
    mem_end = ram_start + ram_size;
    total_pages = ram_size >> PAGE_SHIFT;

    serial::puts("[pmm] RAM: ");
    serial::put_hex(mem_start);
    serial::puts(" - ");
    serial::put_hex(mem_end);
    serial::puts(" (");
    serial::put_dec(ram_size / (1024 * 1024));
    serial::puts(" MB)\n");

    bitmap_size = (total_pages + 63) / 64;
    u64 bitmap_bytes = bitmap_size * sizeof(u64);
    u64 bitmap_addr = page_align_up(kernel_end);
    u64 usable_start = page_align_up(bitmap_addr + bitmap_bytes);

    u64 fb_start = (fb_base != 0) ? fb_base : kc::mem::FB_BASE;
    u64 fb_size = (fb_size_param != 0) ? fb_size_param : kc::mem::FB_SIZE;
    u64 fb_end = fb_start + fb_size;

    serial::puts("[pmm] Framebuffer: ");
    serial::put_hex(fb_start);
    serial::puts(" (");
    serial::put_dec(fb_size / (1024 * 1024));
    serial::puts(" MB)\n");

    init_buddy_region(fb_end);
    init_bitmap_region(bitmap_addr, usable_start, fb_start);
    print_summary();
}

/** @copydoc pmm::alloc_page */
u64 alloc_page() {
    // Try buddy allocator first (larger region)
    if (buddy_available) {
        u64 addr = mm::buddy::get_allocator().alloc_page();
        if (addr != 0) {
            return addr;
        }
        // Buddy is exhausted, fall through to bitmap
    }

    // Fall back to bitmap allocator (pre-framebuffer region)
    SpinlockGuard guard(pmm_lock);

    // Start search from next_free_hint to avoid O(n) scans
    u64 start_word = next_free_hint;
    for (u64 i = 0; i < bitmap_size; i++) {
        u64 word = (start_word + i) % bitmap_size;
        if (bitmap[word] != ~0ULL) {
            // Found a word with at least one free bit
            for (u64 bit = 0; bit < 64; bit++) {
                u64 page = word * 64 + bit;
                if (page >= total_pages)
                    break;

                if (!test_bit(page)) {
                    set_bit(page);
                    free_count--;
                    // Update hint to this word for next allocation
                    next_free_hint = word;
                    return page_to_addr(page);
                }
            }
        }
    }

    serial::puts("[pmm] ERROR: Out of physical memory!\n");
    return 0;
}

/** @copydoc pmm::alloc_pages */
u64 alloc_pages(u64 count) {
    if (count == 0)
        return 0;
    if (count == 1)
        return alloc_page();

    // Try buddy allocator first (larger region, O(log n) for contiguous)
    if (buddy_available) {
        u32 order = mm::buddy::pages_to_order(count);
        u64 addr = mm::buddy::get_allocator().alloc_pages(order);
        if (addr != 0) {
            return addr;
        }
        // Buddy is exhausted, fall through to bitmap
    }

    // Fall back to bitmap allocator (pre-framebuffer region)
    SpinlockGuard guard(pmm_lock);

    u64 run_start = 0;
    u64 run_length = 0;

    // Start from hint for contiguous allocations too
    u64 start_page = next_free_hint * 64;
    if (start_page >= total_pages) start_page = 0;

    for (u64 i = 0; i < total_pages; i++) {
        u64 page = (start_page + i) % total_pages;
        if (!test_bit(page)) {
            if (run_length == 0) {
                run_start = page;
            }
            run_length++;

            if (run_length == count) {
                // Found enough contiguous pages
                for (u64 j = 0; j < count; j++) {
                    set_bit(run_start + j);
                }
                free_count -= count;
                // Update hint
                next_free_hint = (run_start + count) / 64;
                if (next_free_hint >= bitmap_size) next_free_hint = 0;
                return page_to_addr(run_start);
            }
        } else {
            run_length = 0;
        }
    }

    serial::puts("[pmm] ERROR: Cannot allocate ");
    serial::put_dec(count);
    serial::puts(" contiguous pages!\n");
    return 0;
}

/** @copydoc pmm::free_page */
void free_page(u64 phys_addr) {
    // Determine which allocator owns this page based on address
    if (buddy_available && phys_addr >= buddy_region_start && phys_addr < buddy_region_end) {
        mm::buddy::get_allocator().free_page(phys_addr);
        return;
    }

    // Must be from bitmap allocator (pre-framebuffer region)
    SpinlockGuard guard(pmm_lock);
    free_page_unlocked(phys_addr);
}

/** @copydoc pmm::free_pages */
void free_pages(u64 phys_addr, u64 count) {
    // For simplicity, we determine ownership of the first page
    // and assume all pages in the range belong to the same allocator
    // (This is safe as long as allocations don't cross regions)

    if (buddy_available && phys_addr >= buddy_region_start && phys_addr < buddy_region_end) {
        // Pages from buddy allocator - free one at a time
        // (We don't track allocation order, so can't coalesce efficiently)
        for (u64 i = 0; i < count; i++) {
            mm::buddy::get_allocator().free_page(phys_addr + i * PAGE_SIZE);
        }
        return;
    }

    // Pages from bitmap allocator
    SpinlockGuard guard(pmm_lock);

    for (u64 i = 0; i < count; i++) {
        free_page_unlocked(phys_addr + i * PAGE_SIZE);
    }
}

/** @copydoc pmm::get_total_pages */
u64 get_total_pages() {
    // Always return the full RAM page count for "total memory" reporting.
    // This gives a consistent and accurate number for sysinfo.
    // The individual allocators (buddy + bitmap) manage different regions,
    // but the user cares about total system RAM.
    SpinlockGuard guard(pmm_lock);
    return total_pages;
}

/** @copydoc pmm::get_free_pages */
u64 get_free_pages() {
    SpinlockGuard guard(pmm_lock);

    u64 total_free = 0;

    if (buddy_available) {
        total_free += mm::buddy::get_allocator().free_pages_count();
    }

    // Add bitmap free pages (pre-framebuffer region)
    total_free += free_count;

    return total_free;
}

/** @copydoc pmm::get_used_pages */
u64 get_used_pages() {
    SpinlockGuard guard(pmm_lock);

    u64 total_used = 0;

    if (buddy_available) {
        auto &alloc = mm::buddy::get_allocator();
        total_used += alloc.total_pages() - alloc.free_pages_count();
    }

    // Add bitmap used pages
    // Note: We track free_count, not total usable pre-FB pages,
    // so this may not be perfectly accurate but is conservative.

    return total_used;
}

} // namespace pmm
