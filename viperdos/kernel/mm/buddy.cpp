//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file buddy.cpp
 * @brief Buddy allocator implementation.
 *
 * @details
 * The buddy allocator works by maintaining free lists for each power-of-two
 * block size. Allocation finds the smallest order with a free block, splits
 * larger blocks if needed. Deallocation checks if the buddy is also free and
 * coalesces blocks to reduce fragmentation.
 *
 * Performance optimization: Per-CPU page cache for order-0 allocations.
 * This reduces lock contention by caching single pages per-CPU.
 */

#include "buddy.hpp"
#include "../arch/aarch64/cpu.hpp"
#include "../console/serial.hpp"

namespace mm::buddy {

namespace {
// Global buddy allocator instance
BuddyAllocator g_allocator;

// =============================================================================
// Per-CPU Page Cache for reduced lock contention
// =============================================================================

/// Maximum pages cached per CPU (keep small to limit memory overhead)
constexpr u32 PERCPU_CACHE_SIZE = 8;

/// Per-CPU page cache structure
struct PerCpuPageCache {
    u64 pages[PERCPU_CACHE_SIZE]; ///< Cached page addresses
    u32 count;                    ///< Number of pages in cache
    Spinlock lock;                ///< Per-CPU lock (lightweight)
};

/// Per-CPU caches (one per CPU)
PerCpuPageCache g_percpu_cache[cpu::MAX_CPUS];

/// Whether per-CPU caching is enabled (set after buddy init)
bool g_percpu_cache_enabled = false;

/**
 * @brief Try to allocate a page from the per-CPU cache.
 * @return Page address, or 0 if cache is empty.
 */
u64 percpu_cache_alloc() {
    if (!g_percpu_cache_enabled) {
        return 0;
    }

    u32 cpu_id = cpu::current_id();
    if (cpu_id >= cpu::MAX_CPUS) {
        return 0;
    }

    PerCpuPageCache &cache = g_percpu_cache[cpu_id];
    SpinlockGuard guard(cache.lock);

    if (cache.count == 0) {
        return 0;
    }

    cache.count--;
    return cache.pages[cache.count];
}

/**
 * @brief Try to free a page to the per-CPU cache.
 * @param addr Page address to cache.
 * @return true if cached, false if cache is full.
 */
bool percpu_cache_free(u64 addr) {
    if (!g_percpu_cache_enabled) {
        return false;
    }

    u32 cpu_id = cpu::current_id();
    if (cpu_id >= cpu::MAX_CPUS) {
        return false;
    }

    PerCpuPageCache &cache = g_percpu_cache[cpu_id];
    SpinlockGuard guard(cache.lock);

    if (cache.count >= PERCPU_CACHE_SIZE) {
        return false;
    }

    cache.pages[cache.count] = addr;
    cache.count++;
    return true;
}

} // namespace

BuddyAllocator &get_allocator() {
    return g_allocator;
}

bool BuddyAllocator::init(u64 mem_start, u64 mem_end, u64 reserved_end) {
    SpinlockGuard guard(lock_);

    if (initialized_) {
        serial::puts("[buddy] Already initialized\n");
        return false;
    }

    // Align boundaries
    mem_start_ = (mem_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    mem_end_ = mem_end & ~(PAGE_SIZE - 1);
    reserved_end = (reserved_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (mem_end_ <= mem_start_ || reserved_end >= mem_end_) {
        serial::puts("[buddy] Invalid memory range\n");
        return false;
    }

    total_pages_ = (mem_end_ - mem_start_) >> PAGE_SHIFT;

    // Initialize free lists
    for (u32 i = 0; i < MAX_ORDER; i++) {
        free_areas_[i].free_list = nullptr;
        free_areas_[i].count = 0;
    }

    serial::puts("[buddy] Initializing: ");
    serial::put_hex(mem_start_);
    serial::puts(" - ");
    serial::put_hex(mem_end_);
    serial::puts(" (");
    serial::put_dec(total_pages_);
    serial::puts(" pages)\n");

    serial::puts("[buddy] Reserved up to: ");
    serial::put_hex(reserved_end);
    serial::puts("\n");

    // Add free pages to the allocator, starting from reserved_end
    // We try to create the largest possible blocks

    u64 addr = reserved_end;
    while (addr < mem_end_) {
        // Find the largest order block we can create at this address
        // that doesn't exceed mem_end_
        u32 order = MAX_ORDER - 1;

        while (order > 0) {
            u64 block_size = PAGE_SIZE << order;

            // Check alignment: address must be aligned to block size
            if ((addr & (block_size - 1)) != 0) {
                order--;
                continue;
            }

            // Check that block fits in remaining space
            if (addr + block_size > mem_end_) {
                order--;
                continue;
            }

            break;
        }

        // Add this block to the free list
        u64 block_size = PAGE_SIZE << order;
        add_to_free_list(addr, order);
        addr += block_size;
    }

    initialized_ = true;

    // Enable per-CPU page caching now that the buddy allocator is ready
    g_percpu_cache_enabled = true;

    serial::puts("[buddy] Initialized with ");
    serial::put_dec(free_pages_count());
    serial::puts(" free pages\n");

    return true;
}

u64 BuddyAllocator::alloc_pages(u32 order) {
    if (order >= MAX_ORDER) {
        serial::puts("[buddy] Order too large: ");
        serial::put_dec(order);
        serial::puts("\n");
        return 0;
    }

    // Fast path: try per-CPU cache for single page allocations
    if (order == 0) {
        u64 cached = percpu_cache_alloc();
        if (cached != 0) {
            return cached;
        }
    }

    SpinlockGuard guard(lock_);

    if (!initialized_) {
        serial::puts("[buddy] Not initialized\n");
        return 0;
    }

    // Find smallest order with a free block
    u32 current_order = order;
    while (current_order < MAX_ORDER && free_areas_[current_order].count == 0) {
        current_order++;
    }

    if (current_order >= MAX_ORDER) {
        serial::puts("[buddy] Out of memory for order ");
        serial::put_dec(order);
        serial::puts("\n");
        return 0;
    }

    // Split larger blocks down to the requested order
    while (current_order > order) {
        split_block(current_order);
        current_order--;
    }

    // Pop a block from the free list
    u64 addr = pop_from_free_list(order);
    if (addr == 0) {
        serial::puts("[buddy] Failed to pop block for order ");
        serial::put_dec(order);
        serial::puts("\n");
        return 0;
    }

    return addr;
}

void BuddyAllocator::free_pages(u64 addr, u32 order) {
    if (order >= MAX_ORDER) {
        serial::puts("[buddy] Invalid order in free: ");
        serial::put_dec(order);
        serial::puts("\n");
        return;
    }

    // Fast path: try per-CPU cache for single page deallocations
    if (order == 0 && percpu_cache_free(addr)) {
        return;
    }

    SpinlockGuard guard(lock_);

    if (!initialized_) {
        serial::puts("[buddy] Not initialized\n");
        return;
    }

    // Validate address range
    if (addr < mem_start_ || addr >= mem_end_) {
        serial::puts("[buddy] Invalid address in free: ");
        serial::put_hex(addr);
        serial::puts("\n");
        return;
    }

    // Validate alignment
    u64 block_size = PAGE_SIZE << order;
    if ((addr & (block_size - 1)) != 0) {
        serial::puts("[buddy] Misaligned address in free: ");
        serial::put_hex(addr);
        serial::puts(" order=");
        serial::put_dec(order);
        serial::puts("\n");
        return;
    }

    // Double-free detection: check if block is already in a free list
    for (u32 o = 0; o < MAX_ORDER; o++) {
        FreeBlock *blk = free_areas_[o].free_list;
        while (blk) {
            if (reinterpret_cast<u64>(blk) == addr) {
                serial::puts("[buddy] WARNING: double-free detected at ");
                serial::put_hex(addr);
                serial::puts(" order=");
                serial::put_dec(order);
                serial::puts(", already free at order=");
                serial::put_dec(o);
                serial::puts("\n");
                return;
            }
            blk = blk->next;
        }
    }

    // Add to free list and try to coalesce
    try_coalesce(addr, order);
}

u64 BuddyAllocator::free_pages_count() const {
    // Note: We don't acquire the lock here because this function may be
    // called from contexts where the lock is already held (e.g., during init).
    // The caller is responsible for ensuring thread safety if needed.
    // For statistics queries, a slightly stale value is acceptable.

    if (!initialized_) {
        return 0;
    }

    u64 total = 0;
    for (u32 i = 0; i < MAX_ORDER; i++) {
        total += free_areas_[i].count << i; // count * (2^order)
    }
    return total;
}

void BuddyAllocator::dump() const {
    serial::puts("[buddy] Allocator state:\n");
    serial::puts("  Memory: ");
    serial::put_hex(mem_start_);
    serial::puts(" - ");
    serial::put_hex(mem_end_);
    serial::puts("\n");
    serial::puts("  Total pages: ");
    serial::put_dec(total_pages_);
    serial::puts("\n");
    serial::puts("  Free pages: ");
    serial::put_dec(free_pages_count());
    serial::puts("\n");
    serial::puts("  Free lists:\n");

    for (u32 i = 0; i < MAX_ORDER; i++) {
        if (free_areas_[i].count > 0) {
            serial::puts("    Order ");
            serial::put_dec(i);
            serial::puts(" (");
            serial::put_dec(1ULL << i);
            serial::puts(" pages): ");
            serial::put_dec(free_areas_[i].count);
            serial::puts(" blocks\n");
        }
    }
}

void BuddyAllocator::add_to_free_list(u64 addr, u32 order) {
    FreeBlock *block = reinterpret_cast<FreeBlock *>(addr);
    block->order = order;
    block->next = free_areas_[order].free_list;
    free_areas_[order].free_list = block;
    free_areas_[order].count++;
}

bool BuddyAllocator::remove_from_free_list(u64 addr, u32 order) {
    FreeBlock **pp = &free_areas_[order].free_list;

    while (*pp != nullptr) {
        if (reinterpret_cast<u64>(*pp) == addr) {
            *pp = (*pp)->next;
            free_areas_[order].count--;
            return true;
        }
        pp = &((*pp)->next);
    }

    return false;
}

u64 BuddyAllocator::pop_from_free_list(u32 order) {
    FreeBlock *block = free_areas_[order].free_list;
    if (block == nullptr) {
        return 0;
    }

    free_areas_[order].free_list = block->next;
    free_areas_[order].count--;

    return reinterpret_cast<u64>(block);
}

void BuddyAllocator::try_coalesce(u64 addr, u32 order) {
    while (order < MAX_ORDER - 1) {
        u64 buddy_addr = get_buddy_addr(addr, order);

        // Check if buddy is within our memory range
        if (buddy_addr < mem_start_ || buddy_addr >= mem_end_) {
            // Can't coalesce, just add to free list
            add_to_free_list(addr, order);
            return;
        }

        // Check if buddy is in the free list
        if (!remove_from_free_list(buddy_addr, order)) {
            // Buddy is not free, just add to free list
            add_to_free_list(addr, order);
            return;
        }

        // Buddy was free - coalesce into larger block
        // The combined block starts at the lower address
        addr = (addr < buddy_addr) ? addr : buddy_addr;
        order++;
    }

    // Reached max order, add to free list
    add_to_free_list(addr, order);
}

void BuddyAllocator::split_block(u32 order) {
    if (order == 0 || order >= MAX_ORDER)
        return;

    // Pop a block from this order
    u64 addr = pop_from_free_list(order);
    if (addr == 0)
        return;

    // Split into two blocks of order-1
    u32 lower_order = order - 1;
    u64 block_size = PAGE_SIZE << lower_order;

    // Add both halves to the lower order free list
    add_to_free_list(addr, lower_order);
    add_to_free_list(addr + block_size, lower_order);
}

} // namespace mm::buddy
