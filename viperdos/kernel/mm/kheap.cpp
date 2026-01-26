#include "kheap.hpp"
#include "../arch/aarch64/cpu.hpp"
#include "../console/serial.hpp"
#include "../include/constants.hpp"
#include "../lib/spinlock.hpp"
#include "pmm.hpp"

// Suppress warning for DEBUG_MODE which is set by preprocessor
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"

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

namespace kheap {

namespace {
// Debug configuration
#ifdef KHEAP_DEBUG
constexpr bool DEBUG_MODE = true;
#else
constexpr bool DEBUG_MODE = false;
#endif

// Magic numbers for block validation (from constants.hpp)
constexpr u32 BLOCK_MAGIC_ALLOC = kc::magic::HEAP_ALLOCATED;
constexpr u32 BLOCK_MAGIC_FREE = kc::magic::HEAP_FREED;
constexpr u32 BLOCK_MAGIC_POISON = kc::magic::HEAP_POISONED;

// Block header structure with magic number for validation
struct BlockHeader {
    u32 magic;          // Magic number for corruption detection
    u32 _pad;           // Padding for alignment
    u64 size_and_flags; // Size in bytes (including header), bit 0 = in_use

    bool is_free() const {
        return (size_and_flags & 1) == 0;
    }

    void set_free() {
        size_and_flags &= ~1ULL;
        magic = BLOCK_MAGIC_FREE;
    }

    void set_used() {
        size_and_flags |= 1;
        magic = BLOCK_MAGIC_ALLOC;
    }

    bool is_valid() const {
        return magic == BLOCK_MAGIC_ALLOC || magic == BLOCK_MAGIC_FREE;
    }

    bool is_poisoned() const {
        return magic == BLOCK_MAGIC_POISON;
    }

    void poison() {
        magic = BLOCK_MAGIC_POISON;
    }

    u64 size() const {
        return size_and_flags & ~1ULL;
    }

    void set_size(u64 s) {
        size_and_flags = (size_and_flags & 1) | (s & ~1ULL);
    }

    // Get pointer to user data
    void *data() {
        return reinterpret_cast<u8 *>(this) + sizeof(BlockHeader);
    }

    // Get next block in memory (based on size)
    BlockHeader *next_in_memory() {
        return reinterpret_cast<BlockHeader *>(reinterpret_cast<u8 *>(this) + size());
    }
};

// Free block has a next pointer stored in the data area
struct FreeBlock {
    BlockHeader header;
    FreeBlock *next; // Next in free list
};

// Minimum allocation size (header + enough for next pointer when freed)
constexpr u64 MIN_BLOCK_SIZE = sizeof(FreeBlock);
constexpr u64 HEADER_SIZE = sizeof(BlockHeader);
constexpr u64 ALIGNMENT = 16;
constexpr u64 MAX_HEAP_SIZE = 64 * 1024 * 1024; // 64 MB max

// Heap region tracking for non-contiguous allocations
struct HeapRegion {
    u64 start;
    u64 end;
};

constexpr usize MAX_HEAP_REGIONS = 16;
HeapRegion heap_regions[MAX_HEAP_REGIONS];
usize heap_region_count = 0;

// Heap state
u64 heap_start = 0;
u64 heap_end = 0;
u64 heap_size = 0;

// Segregated free lists by size class for O(1) small allocations
// Size classes: 32, 64, 128, 256, 512, 1024, 2048, 4096, >4096 (large)
constexpr usize NUM_SIZE_CLASSES = 9;
constexpr u64 SIZE_CLASS_LIMITS[NUM_SIZE_CLASSES] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096, ~0ULL // Last is "large"
};
FreeBlock *free_lists[NUM_SIZE_CLASSES] = {nullptr};
u64 free_list_counts[NUM_SIZE_CLASSES] = {0};

// Per-CPU arenas for lock-free small allocations
// Each CPU has its own free lists for the smaller size classes (0-5, up to 1024 bytes)
constexpr usize PERCPU_SIZE_CLASSES = 6; // 32, 64, 128, 256, 512, 1024
constexpr usize PERCPU_CACHE_SIZE = 8;   // Max blocks to cache per size class per CPU

struct PerCpuArena {
    FreeBlock *free_lists[PERCPU_SIZE_CLASSES];
    u32 counts[PERCPU_SIZE_CLASSES];
    Spinlock lock; // Per-CPU lock (rarely contended)
    bool initialized;
};

PerCpuArena percpu_arenas[cpu::MAX_CPUS];
bool percpu_enabled = false;

// Legacy single free list pointer for backward compatibility with dump()
FreeBlock *free_list = nullptr;

u64 total_allocated = 0;
u64 total_free = 0;
u64 free_block_count = 0;

// Spinlock for thread safety
Spinlock heap_lock;

/**
 * @brief Get the size class index for a given block size.
 */
inline usize get_size_class(u64 size) {
    for (usize i = 0; i < NUM_SIZE_CLASSES - 1; i++) {
        if (size <= SIZE_CLASS_LIMITS[i]) {
            return i;
        }
    }
    return NUM_SIZE_CLASSES - 1; // Large blocks
}

/**
 * @brief Check if an address falls within any heap region.
 */
bool is_in_heap(u64 addr) {
    for (usize i = 0; i < heap_region_count; i++) {
        if (addr >= heap_regions[i].start && addr < heap_regions[i].end) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Add a new heap region to the tracking array.
 */
bool add_heap_region(u64 start, u64 end) {
    if (heap_region_count >= MAX_HEAP_REGIONS) {
        serial::puts("[kheap] ERROR: Too many heap regions\n");
        return false;
    }
    heap_regions[heap_region_count].start = start;
    heap_regions[heap_region_count].end = end;
    heap_region_count++;
    return true;
}

/**
 * @brief Align a value up to the next multiple of ALIGNMENT.
 */
inline u64 align_up(u64 value) {
    return (value + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

/**
 * @brief Convert a user pointer to its block header.
 */
inline BlockHeader *ptr_to_header(void *ptr) {
    return reinterpret_cast<BlockHeader *>(reinterpret_cast<u8 *>(ptr) - HEADER_SIZE);
}

/**
 * @brief Expand the heap by allocating more pages.
 */
bool expand_heap(u64 needed) {
    if (heap_size + needed > MAX_HEAP_SIZE) {
        serial::puts("[kheap] ERROR: Would exceed maximum heap size\n");
        return false;
    }

    u64 pages_needed = (needed + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    u64 new_pages = pmm::alloc_pages(pages_needed);

    if (new_pages == 0) {
        serial::puts("[kheap] ERROR: Failed to allocate pages for heap expansion\n");
        return false;
    }

    u64 expansion_size = pages_needed * pmm::PAGE_SIZE;

    // Check if contiguous with existing heap
    if (new_pages == heap_end) {
        // Contiguous - extend the last region
        heap_end += expansion_size;
        heap_size += expansion_size;

        // Update the last heap region's end
        if (heap_region_count > 0) {
            heap_regions[heap_region_count - 1].end = heap_end;
        }

        // Create a free block for the new space and add to segregated list
        FreeBlock *new_block = reinterpret_cast<FreeBlock *>(new_pages);
        new_block->header.magic = BLOCK_MAGIC_FREE;
        new_block->header._pad = 0;
        new_block->header.size_and_flags = expansion_size; // Free (bit 0 = 0)

        // Add to large size class
        usize class_idx = get_size_class(expansion_size);
        new_block->next = free_lists[class_idx];
        free_lists[class_idx] = new_block;
        free_list_counts[class_idx]++;
        total_free += expansion_size;
        free_block_count++;

        // Update legacy pointer
        if (free_list == nullptr) free_list = new_block;

        return true;
    } else {
        // Non-contiguous - create new heap region
        serial::puts("[kheap] Non-contiguous heap expansion at ");
        serial::put_hex(new_pages);
        serial::puts(" (");
        serial::put_dec(expansion_size / 1024);
        serial::puts(" KB)\n");

        // Register this as a new heap region
        if (!add_heap_region(new_pages, new_pages + expansion_size)) {
            // Failed to add region - free the allocated pages to prevent leak
            pmm::free_pages(new_pages, pages_needed);
            serial::puts("[kheap] ERROR: Failed to track heap region\n");
            return false;
        }

        heap_size += expansion_size;

        // Add as a new free block to segregated list
        FreeBlock *new_block = reinterpret_cast<FreeBlock *>(new_pages);
        new_block->header.magic = BLOCK_MAGIC_FREE;
        new_block->header._pad = 0;
        new_block->header.size_and_flags = expansion_size;

        // Add to large size class
        usize class_idx = get_size_class(expansion_size);
        new_block->next = free_lists[class_idx];
        free_lists[class_idx] = new_block;
        free_list_counts[class_idx]++;
        total_free += expansion_size;
        free_block_count++;

        // Update legacy pointer
        if (free_list == nullptr) free_list = new_block;

        return true;
    }
}

/**
 * @brief Add a block to the appropriate segregated free list.
 */
void add_to_free_list(FreeBlock *block) {
    block->header.set_free();

    u64 size = block->header.size();

    // Validate the block before adding
    u64 block_addr = reinterpret_cast<u64>(block);
    if (!is_in_heap(block_addr)) {
        serial::puts("[kheap] ERROR: Trying to add invalid block ");
        serial::put_hex(block_addr);
        serial::puts(" to free list\n");
        return;
    }

    usize class_idx = get_size_class(size);

    // Insert sorted by address within the size class for easier coalescing
    FreeBlock **pp = &free_lists[class_idx];
    while (*pp != nullptr && *pp < block) {
        // Validate each node as we traverse
        u64 node_addr = reinterpret_cast<u64>(*pp);
        if (!is_in_heap(node_addr)) {
            serial::puts("[kheap] CORRUPTION in add: next ptr ");
            serial::put_hex(node_addr);
            serial::puts(" invalid, breaking chain\n");
            *pp = nullptr;
            break;
        }
        pp = &((*pp)->next);
    }
    block->next = *pp;
    *pp = block;
    free_list_counts[class_idx]++;
    free_block_count++;

    // Update legacy free_list pointer to first non-empty list
    free_list = nullptr;
    for (usize i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (free_lists[i] != nullptr) {
            free_list = free_lists[i];
            break;
        }
    }
}

/**
 * @brief Remove a block from its size class free list.
 */
void remove_from_free_list(FreeBlock *block) {
    u64 size = block->header.size();
    usize class_idx = get_size_class(size);

    FreeBlock **pp = &free_lists[class_idx];
    while (*pp != nullptr) {
        if (*pp == block) {
            *pp = block->next;
            free_list_counts[class_idx]--;
            free_block_count--;
            return;
        }
        pp = &((*pp)->next);
    }
}

/**
 * @brief Coalesce adjacent free blocks across all size classes.
 * This is called after kfree to merge adjacent blocks.
 */
void coalesce() {
    // For each size class, check if blocks can be merged
    // Since blocks are sorted by address within each class,
    // we also need to check across classes for adjacent blocks

    // Build a temporary merged list of all free blocks sorted by address
    // This is O(n) but coalescing is called infrequently

    // Simple approach: for the just-freed block, check its neighbors
    // More sophisticated: periodic full coalesce

    // For now, do per-class coalescing which handles most cases
    for (usize c = 0; c < NUM_SIZE_CLASSES; c++) {
        FreeBlock *current = free_lists[c];
        while (current != nullptr && current->next != nullptr) {
            // Check if current and next are adjacent in memory
            u8 *current_end = reinterpret_cast<u8 *>(current) + current->header.size();
            if (current_end == reinterpret_cast<u8 *>(current->next)) {
                // Merge current with next
                u64 combined_size = current->header.size() + current->next->header.size();
                FreeBlock *absorbed = current->next;

                // Remove current from this size class
                remove_from_free_list(current);
                // Remove absorbed from this size class
                remove_from_free_list(absorbed);

                // Update size and re-add to appropriate class
                current->header.set_size(combined_size);
                current->header.set_free();
                add_to_free_list(current);

                // Restart coalescing since lists changed
                current = free_lists[c];
            } else {
                current = current->next;
            }
        }
    }

    // Update legacy free_list pointer
    free_list = nullptr;
    for (usize i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (free_lists[i] != nullptr) {
            free_list = free_lists[i];
            break;
        }
    }
}
} // namespace

void init() {
    serial::puts("[kheap] Initializing kernel heap with free list allocator\n");

    // Allocate initial heap pages (64KB)
    u64 initial_pages = 16;
    u64 first_page = pmm::alloc_pages(initial_pages);

    if (first_page == 0) {
        serial::puts("[kheap] ERROR: Failed to allocate initial heap!\n");
        return;
    }

    heap_start = first_page;
    heap_end = first_page + initial_pages * pmm::PAGE_SIZE;
    heap_size = initial_pages * pmm::PAGE_SIZE;

    // Register the initial heap region
    add_heap_region(heap_start, heap_end);

    // Initialize segregated free lists
    for (usize i = 0; i < NUM_SIZE_CLASSES; i++) {
        free_lists[i] = nullptr;
        free_list_counts[i] = 0;
    }

    // Initialize with one big free block in the large class
    FreeBlock *initial_block = reinterpret_cast<FreeBlock *>(heap_start);
    initial_block->header.magic = BLOCK_MAGIC_FREE;
    initial_block->header._pad = 0;
    initial_block->header.size_and_flags = heap_size; // Free (bit 0 = 0)
    initial_block->next = nullptr;

    // Add to large size class
    free_lists[NUM_SIZE_CLASSES - 1] = initial_block;
    free_list_counts[NUM_SIZE_CLASSES - 1] = 1;
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

    // Initialize per-CPU arenas
    for (u32 i = 0; i < cpu::MAX_CPUS; i++) {
        for (usize j = 0; j < PERCPU_SIZE_CLASSES; j++) {
            percpu_arenas[i].free_lists[j] = nullptr;
            percpu_arenas[i].counts[j] = 0;
        }
        percpu_arenas[i].initialized = true;
    }
    percpu_enabled = true;
    serial::puts("[kheap] Per-CPU arenas enabled\n");
}

void *kmalloc(u64 size) {
    if (size == 0)
        return nullptr;

    // Calculate required block size
    u64 required = align_up(size + HEADER_SIZE);
    if (required < MIN_BLOCK_SIZE) {
        required = MIN_BLOCK_SIZE;
    }

    // Try per-CPU arena first for small allocations (reduces lock contention)
    usize size_class = get_size_class(required);
    if (percpu_enabled && size_class < PERCPU_SIZE_CLASSES) {
        u32 cpu_id = cpu::current_id();
        if (cpu_id < cpu::MAX_CPUS) {
            PerCpuArena &arena = percpu_arenas[cpu_id];
            SpinlockGuard pcpu_guard(arena.lock);

            if (arena.free_lists[size_class] != nullptr) {
                FreeBlock *block = arena.free_lists[size_class];
                arena.free_lists[size_class] = block->next;
                arena.counts[size_class]--;

                block->header.set_used();
                total_allocated += block->header.size();
                return block->header.data();
            }
        }
    }

    // Fall back to global heap
    SpinlockGuard guard(heap_lock);

    // Search segregated free lists starting from the appropriate size class
    FreeBlock *best = nullptr;
    FreeBlock **best_prev = nullptr;
    usize best_class = NUM_SIZE_CLASSES;

    // Search from size_class upward to find a suitable block
    for (usize c = size_class; c < NUM_SIZE_CLASSES; c++) {
        FreeBlock **pp = &free_lists[c];
        while (*pp != nullptr) {
            // Validate the free block before using it
            u64 block_addr = reinterpret_cast<u64>(*pp);
            if (!is_in_heap(block_addr)) {
                serial::puts("[kheap] CORRUPTION: Free list contains invalid addr ");
                serial::put_hex(block_addr);
                serial::puts(" in class ");
                serial::put_dec(c);
                serial::puts("\n");
                // Skip this corrupted entry
                *pp = (*pp)->next;
                free_list_counts[c]--;
                free_block_count--;
                continue;
            }
            if ((*pp)->header.magic != BLOCK_MAGIC_FREE) {
                serial::puts("[kheap] CORRUPTION: Block at ");
                serial::put_hex(block_addr);
                serial::puts(" has bad magic 0x");
                serial::put_hex((*pp)->header.magic);
                serial::puts("\n");
                // Skip this corrupted entry
                *pp = (*pp)->next;
                free_list_counts[c]--;
                free_block_count--;
                continue;
            }
            if ((*pp)->header.size() >= required) {
                best = *pp;
                best_prev = pp;
                best_class = c;
                goto found; // First fit found
            }
            pp = &((*pp)->next);
        }
    }

found:
    // Need to expand heap?
    if (best == nullptr) {
        if (!expand_heap(required)) {
            return nullptr;
        }
        // Try again after expansion - new block will be in large class
        for (usize c = size_class; c < NUM_SIZE_CLASSES; c++) {
            FreeBlock **pp = &free_lists[c];
            while (*pp != nullptr) {
                if ((*pp)->header.size() >= required) {
                    best = *pp;
                    best_prev = pp;
                    best_class = c;
                    goto found2;
                }
                pp = &((*pp)->next);
            }
        }
found2:
        if (best == nullptr) {
            return nullptr;
        }
    }

    u64 block_size = best->header.size();
    u64 remaining = block_size - required;

    // DEBUG: Check if allocation would extend past heap region
    u64 block_addr = reinterpret_cast<u64>(best);
    u64 block_end = block_addr + block_size;
    bool in_valid_region = false;
    for (usize i = 0; i < heap_region_count; i++) {
        if (block_addr >= heap_regions[i].start && block_end <= heap_regions[i].end) {
            in_valid_region = true;
            break;
        }
    }
    if (!in_valid_region && required > 150000) { // Only log for large allocations
        serial::puts("[kheap] WARNING: Block extends past heap region!\n");
        serial::puts("  Block: ");
        serial::put_hex(block_addr);
        serial::puts(" - ");
        serial::put_hex(block_end);
        serial::puts(" (size=");
        serial::put_dec(block_size);
        serial::puts(")\n");
        serial::puts("  Required: ");
        serial::put_dec(required);
        serial::puts("\n");
        serial::puts("  Heap regions:\n");
        for (usize i = 0; i < heap_region_count; i++) {
            serial::puts("    ");
            serial::put_hex(heap_regions[i].start);
            serial::puts(" - ");
            serial::put_hex(heap_regions[i].end);
            serial::puts("\n");
        }
    }

    // Remove from free list (already have pointer to prev)
    *best_prev = best->next;
    free_list_counts[best_class]--;
    free_block_count--;
    total_free -= block_size;

    // Update legacy free_list pointer
    free_list = nullptr;
    for (usize i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (free_lists[i] != nullptr) {
            free_list = free_lists[i];
            break;
        }
    }

    // Split if remaining space is large enough for a free block
    if (remaining >= MIN_BLOCK_SIZE) {
        // Shrink this block
        best->header.set_size(required);
        best->header.set_used();

        // Create new free block from remainder
        FreeBlock *remainder =
            reinterpret_cast<FreeBlock *>(reinterpret_cast<u8 *>(best) + required);
        remainder->header.magic = BLOCK_MAGIC_FREE;
        remainder->header._pad = 0;
        remainder->header.size_and_flags = remaining; // Free
        add_to_free_list(remainder);
        total_free += remaining;

        total_allocated += required;
    } else {
        // Use entire block
        best->header.set_used();
        total_allocated += block_size;
    }

    return best->header.data();
}

void *kzalloc(u64 size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        u8 *p = static_cast<u8 *>(ptr);
        for (u64 i = 0; i < size; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void *krealloc(void *ptr, u64 new_size) {
    if (ptr == nullptr) {
        return kmalloc(new_size);
    }
    if (new_size == 0) {
        kfree(ptr);
        return nullptr;
    }

    // Read old size under lock to prevent race with concurrent free
    u64 old_size;
    {
        SpinlockGuard guard(heap_lock);
        BlockHeader *header = ptr_to_header(ptr);
        if (header->magic != BLOCK_MAGIC_ALLOC) {
            serial::puts("[kheap] ERROR: krealloc on invalid/freed block\n");
            return nullptr;
        }
        old_size = header->size() - HEADER_SIZE;
    }

    // If new size fits in current block, just return
    if (new_size <= old_size) {
        return ptr;
    }

    // Allocate new block (kmalloc handles its own locking)
    void *new_ptr = kmalloc(new_size);
    if (new_ptr == nullptr) {
        return nullptr;
    }

    // Copy old data
    u8 *src = static_cast<u8 *>(ptr);
    u8 *dst = static_cast<u8 *>(new_ptr);
    for (u64 i = 0; i < old_size; i++) {
        dst[i] = src[i];
    }

    // Free old block (kfree handles its own locking)
    kfree(ptr);

    return new_ptr;
}

void kfree(void *ptr) {
    if (ptr == nullptr)
        return;

    // Get block header for size check
    BlockHeader *header = ptr_to_header(ptr);

    // Try to return small blocks to per-CPU arena (reduces lock contention)
    if (percpu_enabled && header->is_valid() && !header->is_free()) {
        u64 block_size = header->size();
        usize size_class = get_size_class(block_size);

        if (size_class < PERCPU_SIZE_CLASSES) {
            u32 cpu_id = cpu::current_id();
            if (cpu_id < cpu::MAX_CPUS) {
                PerCpuArena &arena = percpu_arenas[cpu_id];
                SpinlockGuard pcpu_guard(arena.lock);

                // Only cache if below limit to avoid memory hoarding
                if (arena.counts[size_class] < PERCPU_CACHE_SIZE) {
                    header->set_free();
                    FreeBlock *block = reinterpret_cast<FreeBlock *>(header);
                    block->next = arena.free_lists[size_class];
                    arena.free_lists[size_class] = block;
                    arena.counts[size_class]++;
                    total_allocated -= block_size;
                    return;
                }
            }
        }
    }

    // Fall back to global heap
    SpinlockGuard guard(heap_lock);

    // Bounds check: verify pointer is within any heap region
    u64 addr = reinterpret_cast<u64>(ptr);
    if (!is_in_heap(addr)) {
        serial::puts("[kheap] ERROR: kfree() on invalid pointer ");
        serial::put_hex(addr);
        serial::puts(" (outside all heap regions)\n");
        return;
    }

    // Note: header was already computed at the start of kfree()

    // Alignment check
    if ((reinterpret_cast<u64>(header) % ALIGNMENT) != 0) {
        serial::puts("[kheap] ERROR: kfree() on misaligned pointer ");
        serial::put_hex(addr);
        serial::puts("\n");
        return;
    }

    // Check for corrupted magic number
    if (!header->is_valid()) {
        if (header->is_poisoned()) {
            serial::puts("[kheap] ERROR: Triple-free or use-after-free at ");
            serial::put_hex(addr);
            serial::puts(" (block was already poisoned)\n");
        } else {
            serial::puts("[kheap] ERROR: Heap corruption at ");
            serial::put_hex(addr);
            serial::puts(" (invalid magic 0x");
            serial::put_hex(header->magic);
            serial::puts(")\n");
        }
        return;
    }

    // Double-free check
    if (header->is_free()) {
        serial::puts("[kheap] ERROR: Double-free detected at ");
        serial::put_hex(addr);
        serial::puts(" (size=");
        serial::put_dec(header->size());
        serial::puts(")\n");
        // Poison the block to detect future double-frees
        header->poison();
        return;
    }

    u64 block_size = header->size();

    // Size sanity check
    if (block_size < MIN_BLOCK_SIZE || block_size > heap_size) {
        serial::puts("[kheap] ERROR: Invalid block size ");
        serial::put_dec(block_size);
        serial::puts(" at ");
        serial::put_hex(addr);
        serial::puts("\n");
        return;
    }

    total_allocated -= block_size;
    total_free += block_size;

    // Add to free list
    FreeBlock *block = reinterpret_cast<FreeBlock *>(header);
    add_to_free_list(block);

    // Try to coalesce
    coalesce();
}

u64 get_used() {
    SpinlockGuard guard(heap_lock);
    return total_allocated;
}

u64 get_available() {
    SpinlockGuard guard(heap_lock);
    return total_free;
}

void get_stats(u64 *out_total_size, u64 *out_used, u64 *out_free, u64 *out_free_blocks) {
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

void dump() {
    SpinlockGuard guard(heap_lock);

    serial::puts("[kheap] Heap dump:\n");
    serial::puts("  Regions: ");
    serial::put_dec(heap_region_count);
    serial::puts("\n");
    for (usize i = 0; i < heap_region_count; i++) {
        serial::puts("    [");
        serial::put_dec(i);
        serial::puts("] ");
        serial::put_hex(heap_regions[i].start);
        serial::puts(" - ");
        serial::put_hex(heap_regions[i].end);
        serial::puts("\n");
    }
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
    while (block != nullptr && count < 10) {
        serial::puts("    ");
        serial::put_hex(reinterpret_cast<u64>(block));
        serial::puts(" size=");
        serial::put_dec(block->header.size());
        serial::puts("\n");
        block = block->next;
        count++;
    }
    if (block != nullptr) {
        serial::puts("    ... (more blocks)\n");
    }
}

} // namespace kheap

// C++ operators
void *operator new(size_t size) {
    return kheap::kmalloc(size);
}

void *operator new[](size_t size) {
    return kheap::kmalloc(size);
}

void operator delete(void *ptr) noexcept {
    kheap::kfree(ptr);
}

void operator delete[](void *ptr) noexcept {
    kheap::kfree(ptr);
}

void operator delete(void *ptr, size_t) noexcept {
    kheap::kfree(ptr);
}

void operator delete[](void *ptr, size_t) noexcept {
    kheap::kfree(ptr);
}

#pragma GCC diagnostic pop
