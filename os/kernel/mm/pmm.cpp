#include "pmm.hpp"
#include "../console/serial.hpp"

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
namespace pmm
{

namespace
{
// Memory region info
u64 mem_start = 0;
u64 mem_end = 0;
u64 total_pages = 0;
u64 free_count = 0;

// Bitmap for tracking page allocation
// Each bit represents one page: 0 = free, 1 = used
u64 *bitmap = nullptr;
u64 bitmap_size = 0; // Size in u64 words

// Helper to set a bit (mark page as used)
/**
 * @brief Mark a page as used in the allocation bitmap.
 *
 * @param page_idx Zero-based page index within the managed RAM range.
 */
inline void set_bit(u64 page_idx)
{
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
inline void clear_bit(u64 page_idx)
{
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
inline bool test_bit(u64 page_idx)
{
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
inline u64 addr_to_page(u64 addr)
{
    return (addr - mem_start) >> PAGE_SHIFT;
}

// Convert page index to physical address
/**
 * @brief Convert a PMM page index into a physical base address.
 *
 * @param page_idx Zero-based page index.
 * @return Physical base address of the page.
 */
inline u64 page_to_addr(u64 page_idx)
{
    return mem_start + (page_idx << PAGE_SHIFT);
}
} // namespace

/** @copydoc pmm::init */
void init(u64 ram_start, u64 ram_size, u64 kernel_end)
{
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
    serial::puts(" MB, ");
    serial::put_dec(total_pages);
    serial::puts(" pages)\n");

    // Calculate bitmap size (one bit per page, rounded up to u64)
    bitmap_size = (total_pages + 63) / 64;
    u64 bitmap_bytes = bitmap_size * sizeof(u64);

    // Place bitmap right after kernel
    u64 bitmap_addr = page_align_up(kernel_end);
    bitmap = reinterpret_cast<u64 *>(bitmap_addr);

    serial::puts("[pmm] Bitmap at ");
    serial::put_hex(bitmap_addr);
    serial::puts(" (");
    serial::put_dec(bitmap_bytes);
    serial::puts(" bytes)\n");

    // Initialize all pages as used
    for (u64 i = 0; i < bitmap_size; i++)
    {
        bitmap[i] = ~0ULL;
    }
    free_count = 0;

    // Calculate where usable memory starts (after bitmap)
    u64 usable_start = page_align_up(bitmap_addr + bitmap_bytes);

    // Also reserve space for framebuffer (16MB at 0x41000000)
    // Framebuffer is at fixed address, so mark those pages as used
    u64 fb_start = 0x41000000;
    u64 fb_size = 8 * 1024 * 1024; // 8MB for framebuffer
    u64 fb_end = fb_start + fb_size;

    // Mark usable pages as free (between usable_start and fb_start, and after fb_end)
    for (u64 addr = usable_start; addr < mem_end; addr += PAGE_SIZE)
    {
        // Skip framebuffer region
        if (addr >= fb_start && addr < fb_end)
        {
            continue;
        }
        u64 page = addr_to_page(addr);
        if (page < total_pages)
        {
            clear_bit(page);
            free_count++;
        }
    }

    serial::puts("[pmm] Free pages: ");
    serial::put_dec(free_count);
    serial::puts(" (");
    serial::put_dec((free_count * PAGE_SIZE) / (1024 * 1024));
    serial::puts(" MB)\n");
    serial::puts("[pmm] Reserved: kernel + bitmap + framebuffer\n");
}

/** @copydoc pmm::alloc_page */
u64 alloc_page()
{
    // Simple first-fit allocation
    for (u64 word = 0; word < bitmap_size; word++)
    {
        if (bitmap[word] != ~0ULL)
        {
            // Found a word with at least one free bit
            for (u64 bit = 0; bit < 64; bit++)
            {
                u64 page = word * 64 + bit;
                if (page >= total_pages)
                    break;

                if (!test_bit(page))
                {
                    set_bit(page);
                    free_count--;
                    return page_to_addr(page);
                }
            }
        }
    }

    serial::puts("[pmm] ERROR: Out of physical memory!\n");
    return 0;
}

/** @copydoc pmm::alloc_pages */
u64 alloc_pages(u64 count)
{
    if (count == 0)
        return 0;
    if (count == 1)
        return alloc_page();

    // Find contiguous free pages
    u64 run_start = 0;
    u64 run_length = 0;

    for (u64 page = 0; page < total_pages; page++)
    {
        if (!test_bit(page))
        {
            if (run_length == 0)
            {
                run_start = page;
            }
            run_length++;

            if (run_length == count)
            {
                // Found enough contiguous pages
                for (u64 i = 0; i < count; i++)
                {
                    set_bit(run_start + i);
                }
                free_count -= count;
                return page_to_addr(run_start);
            }
        }
        else
        {
            run_length = 0;
        }
    }

    serial::puts("[pmm] ERROR: Cannot allocate ");
    serial::put_dec(count);
    serial::puts(" contiguous pages!\n");
    return 0;
}

/** @copydoc pmm::free_page */
void free_page(u64 phys_addr)
{
    if (phys_addr < mem_start || phys_addr >= mem_end)
    {
        serial::puts("[pmm] WARNING: Freeing invalid address ");
        serial::put_hex(phys_addr);
        serial::puts("\n");
        return;
    }

    u64 page = addr_to_page(phys_addr);
    if (!test_bit(page))
    {
        serial::puts("[pmm] WARNING: Double-free at ");
        serial::put_hex(phys_addr);
        serial::puts("\n");
        return;
    }

    clear_bit(page);
    free_count++;
}

/** @copydoc pmm::free_pages */
void free_pages(u64 phys_addr, u64 count)
{
    for (u64 i = 0; i < count; i++)
    {
        free_page(phys_addr + i * PAGE_SIZE);
    }
}

/** @copydoc pmm::get_total_pages */
u64 get_total_pages()
{
    return total_pages;
}

/** @copydoc pmm::get_free_pages */
u64 get_free_pages()
{
    return free_count;
}

/** @copydoc pmm::get_used_pages */
u64 get_used_pages()
{
    return total_pages - free_count;
}

} // namespace pmm
