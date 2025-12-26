#include "kheap.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "pmm.hpp"

/**
 * @file kheap.cpp
 * @brief Free-list kernel heap implementation with coalescing.
 *
 * @details
 * This heap uses a first-fit free list allocator with immediate coalescing.
 * Each block (free or allocated) has an 8-byte header containing:
 * - Size of the block (including header), with bit 0 as the "in use" flag
 *
 * The free list is a singly-linked list of free blocks. When a block is freed,
 * we attempt to coalesce it with adjacent free blocks to reduce fragmentation.
 *
 * ## Block Layout
 *
 * ```
 * +----------------+
 * | size | in_use  |  <- 8-byte header (size includes header, bit 0 = in_use)
 * +----------------+
 * | user data...   |  <- returned pointer points here
 * | ...            |
 * +----------------+
 * | next_free      |  <- only present in free blocks (overlaps user data)
 * +----------------+
 * ```
 */

namespace kheap
{

namespace
{
// Block header structure
struct BlockHeader
{
    u64 size_and_flags; // Size in bytes (including header), bit 0 = in_use

    bool is_free() const
    {
        return (size_and_flags & 1) == 0;
    }

    void set_free()
    {
        size_and_flags &= ~1ULL;
    }

    void set_used()
    {
        size_and_flags |= 1;
    }

    u64 size() const
    {
        return size_and_flags & ~1ULL;
    }

    void set_size(u64 s)
    {
        size_and_flags = (size_and_flags & 1) | (s & ~1ULL);
    }

    // Get pointer to user data
    void *data()
    {
        return reinterpret_cast<u8 *>(this) + sizeof(BlockHeader);
    }

    // Get next block in memory (based on size)
    BlockHeader *next_in_memory()
    {
        return reinterpret_cast<BlockHeader *>(reinterpret_cast<u8 *>(this) + size());
    }
};

// Free block has a next pointer stored in the data area
struct FreeBlock
{
    BlockHeader header;
    FreeBlock *next; // Next in free list
};

// Minimum allocation size (header + enough for next pointer when freed)
constexpr u64 MIN_BLOCK_SIZE = sizeof(FreeBlock);
constexpr u64 HEADER_SIZE = sizeof(BlockHeader);
constexpr u64 ALIGNMENT = 16;
constexpr u64 MAX_HEAP_SIZE = 64 * 1024 * 1024; // 64 MB max

// Heap state
u64 heap_start = 0;
u64 heap_end = 0;
u64 heap_size = 0;
FreeBlock *free_list = nullptr;
u64 total_allocated = 0;
u64 total_free = 0;
u64 free_block_count = 0;

// Spinlock for thread safety
Spinlock heap_lock;

/**
 * @brief Align a value up to the next multiple of ALIGNMENT.
 */
inline u64 align_up(u64 value)
{
    return (value + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

/**
 * @brief Convert a user pointer to its block header.
 */
inline BlockHeader *ptr_to_header(void *ptr)
{
    return reinterpret_cast<BlockHeader *>(reinterpret_cast<u8 *>(ptr) - HEADER_SIZE);
}

/**
 * @brief Expand the heap by allocating more pages.
 */
bool expand_heap(u64 needed)
{
    if (heap_size + needed > MAX_HEAP_SIZE)
    {
        serial::puts("[kheap] ERROR: Would exceed maximum heap size\n");
        return false;
    }

    u64 pages_needed = (needed + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    u64 new_pages = pmm::alloc_pages(pages_needed);

    if (new_pages == 0)
    {
        serial::puts("[kheap] ERROR: Failed to allocate pages for heap expansion\n");
        return false;
    }

    // Check if contiguous with existing heap
    if (new_pages == heap_end)
    {
        // Contiguous - extend the last free block or create new one
        u64 expansion_size = pages_needed * pmm::PAGE_SIZE;
        heap_end += expansion_size;
        heap_size += expansion_size;

        // Create a free block for the new space
        FreeBlock *new_block = reinterpret_cast<FreeBlock *>(new_pages);
        new_block->header.size_and_flags = expansion_size; // Free (bit 0 = 0)
        new_block->next = free_list;
        free_list = new_block;
        total_free += expansion_size;
        free_block_count++;

        return true;
    }
    else
    {
        // Non-contiguous - create new heap region
        // This is inefficient but keeps things simple
        serial::puts("[kheap] WARNING: Non-contiguous heap expansion at ");
        serial::put_hex(new_pages);
        serial::puts("\n");

        u64 expansion_size = pages_needed * pmm::PAGE_SIZE;
        heap_size += expansion_size;

        // Add as a new free block
        FreeBlock *new_block = reinterpret_cast<FreeBlock *>(new_pages);
        new_block->header.size_and_flags = expansion_size;
        new_block->next = free_list;
        free_list = new_block;
        total_free += expansion_size;
        free_block_count++;

        return true;
    }
}

/**
 * @brief Add a block to the free list (sorted by address for coalescing).
 */
void add_to_free_list(FreeBlock *block)
{
    block->header.set_free();

    // Insert sorted by address for easier coalescing
    FreeBlock **pp = &free_list;
    while (*pp != nullptr && *pp < block)
    {
        pp = &((*pp)->next);
    }
    block->next = *pp;
    *pp = block;
    free_block_count++;
}

/**
 * @brief Coalesce adjacent free blocks.
 */
void coalesce()
{
    FreeBlock *current = free_list;
    while (current != nullptr && current->next != nullptr)
    {
        // Check if current and next are adjacent in memory
        u8 *current_end = reinterpret_cast<u8 *>(current) + current->header.size();
        if (current_end == reinterpret_cast<u8 *>(current->next))
        {
            // Merge current with next
            u64 combined_size = current->header.size() + current->next->header.size();
            FreeBlock *absorbed = current->next;
            current->header.set_size(combined_size);
            current->next = absorbed->next;
            free_block_count--;
            // Don't advance - check if we can merge again
        }
        else
        {
            current = current->next;
        }
    }
}
} // namespace

void init()
{
    serial::puts("[kheap] Initializing kernel heap with free list allocator\n");

    // Allocate initial heap pages (64KB)
    u64 initial_pages = 16;
    u64 first_page = pmm::alloc_pages(initial_pages);

    if (first_page == 0)
    {
        serial::puts("[kheap] ERROR: Failed to allocate initial heap!\n");
        return;
    }

    heap_start = first_page;
    heap_end = first_page + initial_pages * pmm::PAGE_SIZE;
    heap_size = initial_pages * pmm::PAGE_SIZE;

    // Initialize with one big free block
    FreeBlock *initial_block = reinterpret_cast<FreeBlock *>(heap_start);
    initial_block->header.size_and_flags = heap_size; // Free (bit 0 = 0)
    initial_block->next = nullptr;

    free_list = initial_block;
    total_free = heap_size;
    total_allocated = 0;
    free_block_count = 1;

    serial::puts("[kheap] Heap at ");
    serial::put_hex(heap_start);
    serial::puts(" - ");
    serial::put_hex(heap_end);
    serial::puts(" (");
    serial::put_dec(heap_size / 1024);
    serial::puts(" KB)\n");
}

void *kmalloc(u64 size)
{
    if (size == 0)
        return nullptr;

    SpinlockGuard guard(heap_lock);

    // Calculate required block size (header + aligned user size)
    u64 required = align_up(size + HEADER_SIZE);
    if (required < MIN_BLOCK_SIZE)
    {
        required = MIN_BLOCK_SIZE;
    }

    // First-fit search
    FreeBlock *best = nullptr;
    FreeBlock **best_prev = nullptr;
    FreeBlock **pp = &free_list;

    while (*pp != nullptr)
    {
        if ((*pp)->header.size() >= required)
        {
            best = *pp;
            best_prev = pp;
            break; // First fit
        }
        pp = &((*pp)->next);
    }

    // Need to expand heap?
    if (best == nullptr)
    {
        if (!expand_heap(required))
        {
            return nullptr;
        }
        // Try again after expansion
        pp = &free_list;
        while (*pp != nullptr)
        {
            if ((*pp)->header.size() >= required)
            {
                best = *pp;
                best_prev = pp;
                break;
            }
            pp = &((*pp)->next);
        }
        if (best == nullptr)
        {
            return nullptr;
        }
    }

    u64 block_size = best->header.size();
    u64 remaining = block_size - required;

    // Remove from free list
    *best_prev = best->next;
    free_block_count--;
    total_free -= block_size;

    // Split if remaining space is large enough for a free block
    if (remaining >= MIN_BLOCK_SIZE)
    {
        // Shrink this block
        best->header.set_size(required);
        best->header.set_used();

        // Create new free block from remainder
        FreeBlock *remainder =
            reinterpret_cast<FreeBlock *>(reinterpret_cast<u8 *>(best) + required);
        remainder->header.size_and_flags = remaining; // Free
        add_to_free_list(remainder);
        total_free += remaining;

        total_allocated += required;
    }
    else
    {
        // Use entire block
        best->header.set_used();
        total_allocated += block_size;
    }

    return best->header.data();
}

void *kzalloc(u64 size)
{
    void *ptr = kmalloc(size);
    if (ptr)
    {
        u8 *p = static_cast<u8 *>(ptr);
        for (u64 i = 0; i < size; i++)
        {
            p[i] = 0;
        }
    }
    return ptr;
}

void *krealloc(void *ptr, u64 new_size)
{
    if (ptr == nullptr)
    {
        return kmalloc(new_size);
    }
    if (new_size == 0)
    {
        kfree(ptr);
        return nullptr;
    }

    BlockHeader *header = ptr_to_header(ptr);
    u64 old_size = header->size() - HEADER_SIZE;

    // If new size fits in current block, just return
    if (new_size <= old_size)
    {
        return ptr;
    }

    // Allocate new block
    void *new_ptr = kmalloc(new_size);
    if (new_ptr == nullptr)
    {
        return nullptr;
    }

    // Copy old data
    u8 *src = static_cast<u8 *>(ptr);
    u8 *dst = static_cast<u8 *>(new_ptr);
    for (u64 i = 0; i < old_size; i++)
    {
        dst[i] = src[i];
    }

    // Free old block
    kfree(ptr);

    return new_ptr;
}

void kfree(void *ptr)
{
    if (ptr == nullptr)
        return;

    SpinlockGuard guard(heap_lock);

    BlockHeader *header = ptr_to_header(ptr);

    // Sanity check
    if (header->is_free())
    {
        serial::puts("[kheap] WARNING: Double free detected at ");
        serial::put_hex(reinterpret_cast<u64>(ptr));
        serial::puts("\n");
        return;
    }

    u64 block_size = header->size();
    total_allocated -= block_size;
    total_free += block_size;

    // Add to free list
    FreeBlock *block = reinterpret_cast<FreeBlock *>(header);
    add_to_free_list(block);

    // Try to coalesce
    coalesce();
}

u64 get_used()
{
    SpinlockGuard guard(heap_lock);
    return total_allocated;
}

u64 get_available()
{
    SpinlockGuard guard(heap_lock);
    return total_free;
}

void get_stats(u64 *out_total_size, u64 *out_used, u64 *out_free, u64 *out_free_blocks)
{
    SpinlockGuard guard(heap_lock);
    if (out_total_size)
        *out_total_size = heap_size;
    if (out_used)
        *out_used = total_allocated;
    if (out_free)
        *out_free = total_free;
    if (out_free_blocks)
        *out_free_blocks = free_block_count;
}

void dump()
{
    SpinlockGuard guard(heap_lock);

    serial::puts("[kheap] Heap dump:\n");
    serial::puts("  Range: ");
    serial::put_hex(heap_start);
    serial::puts(" - ");
    serial::put_hex(heap_end);
    serial::puts("\n");
    serial::puts("  Total size: ");
    serial::put_dec(heap_size / 1024);
    serial::puts(" KB\n");
    serial::puts("  Allocated: ");
    serial::put_dec(total_allocated / 1024);
    serial::puts(" KB\n");
    serial::puts("  Free: ");
    serial::put_dec(total_free / 1024);
    serial::puts(" KB\n");
    serial::puts("  Free blocks: ");
    serial::put_dec(free_block_count);
    serial::puts("\n");

    // List free blocks
    serial::puts("  Free list:\n");
    FreeBlock *block = free_list;
    int count = 0;
    while (block != nullptr && count < 10)
    {
        serial::puts("    ");
        serial::put_hex(reinterpret_cast<u64>(block));
        serial::puts(" size=");
        serial::put_dec(block->header.size());
        serial::puts("\n");
        block = block->next;
        count++;
    }
    if (block != nullptr)
    {
        serial::puts("    ... (more blocks)\n");
    }
}

} // namespace kheap

// C++ operators
void *operator new(size_t size)
{
    return kheap::kmalloc(size);
}

void *operator new[](size_t size)
{
    return kheap::kmalloc(size);
}

void operator delete(void *ptr) noexcept
{
    kheap::kfree(ptr);
}

void operator delete[](void *ptr) noexcept
{
    kheap::kfree(ptr);
}

void operator delete(void *ptr, size_t) noexcept
{
    kheap::kfree(ptr);
}

void operator delete[](void *ptr, size_t) noexcept
{
    kheap::kfree(ptr);
}
