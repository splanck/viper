//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "slab.hpp"
#include "../console/serial.hpp"
#include "../lib/mem.hpp"
#include "../lib/spinlock.hpp"
#include "../lib/str.hpp"
#include "pmm.hpp"

/**
 * @file slab.cpp
 * @brief Slab allocator implementation.
 *
 * @details
 * The slab allocator carves fixed-size objects from 4KB pages. Each slab page
 * has a small header at the beginning followed by object slots. Free objects
 * within a slab are linked together using their first 8 bytes as a next pointer.
 *
 * Slab layout (4KB page):
 * +------------------+
 * | Slab header      | (sizeof(Slab) bytes, ~32 bytes)
 * +------------------+
 * | Object 0         |
 * +------------------+
 * | Object 1         |
 * +------------------+
 * | ...              |
 * +------------------+
 * | Object N-1       |
 * +------------------+
 *
 * Free objects are linked through their first pointer-sized word:
 * free_list -> [obj0] -> [obj5] -> [obj2] -> nullptr
 */
namespace slab {

namespace {

// Global cache table
SlabCache caches[MAX_CACHES];
Spinlock slab_lock;
bool initialized = false;

// Pre-defined caches
SlabCache *g_inode_cache = nullptr;
SlabCache *g_task_cache = nullptr;
SlabCache *g_viper_cache = nullptr;
SlabCache *g_channel_cache = nullptr;

/**
 * @brief Find the slab containing a given object pointer.
 *
 * @details
 * Since slabs are page-aligned, we can find the slab header by masking
 * the object address to the page boundary.
 */
Slab *find_slab_for_object(void *ptr) {
    u64 addr = reinterpret_cast<u64>(ptr);
    u64 page_addr = addr & ~(pmm::PAGE_SIZE - 1);
    return reinterpret_cast<Slab *>(page_addr);
}

/**
 * @brief Calculate where objects start in a slab.
 */
void *slab_objects_start(Slab *slab) {
    // Align to 8 bytes after the header
    u64 header_end = reinterpret_cast<u64>(slab) + sizeof(Slab);
    u64 aligned = (header_end + 7) & ~7ULL;
    return reinterpret_cast<void *>(aligned);
}

/**
 * @brief Allocate a new slab for a cache.
 */
Slab *allocate_slab(SlabCache *cache) {
    // Allocate a page from PMM
    u64 phys = pmm::alloc_page();
    if (!phys) {
        serial::puts("[slab] Failed to allocate page for slab\n");
        return nullptr;
    }

    Slab *slab = static_cast<Slab *>(pmm::phys_to_virt(phys));

    // Initialize slab header
    slab->next = nullptr;
    slab->cache = cache; // Set owning cache for O(1) ownership verification
    slab->in_use = 0;
    slab->total = cache->objects_per_slab;

    // Build free list of objects
    u8 *obj_start = static_cast<u8 *>(slab_objects_start(slab));
    slab->free_list = obj_start;

    for (u32 i = 0; i < cache->objects_per_slab; i++) {
        u8 *obj = obj_start + i * cache->object_size;
        void **next_ptr = reinterpret_cast<void **>(obj);

        if (i + 1 < cache->objects_per_slab) {
            *next_ptr = obj_start + (i + 1) * cache->object_size;
        } else {
            *next_ptr = nullptr;
        }
    }

    return slab;
}

/**
 * @brief Free a slab back to the PMM.
 */
void free_slab(Slab *slab) {
    u64 phys = pmm::virt_to_phys(slab);
    pmm::free_page(phys);
}

/**
 * @brief Walk a cache's slab list and free all empty slabs back to PMM.
 *
 * @details
 * Iterates the slab_list, removes any slab with in_use == 0, frees its
 * page, and fixes up the partial_list pointer if it referenced a freed slab.
 *
 * @note Caller must hold cache->lock.
 * @param cache The slab cache to reap from.
 * @return Number of pages reclaimed.
 */
u64 reap_slab_list(SlabCache *cache) {
    u64 pages_reclaimed = 0;

    Slab **prev_ptr = &cache->slab_list;
    Slab *slab = cache->slab_list;

    while (slab) {
        Slab *next = slab->next;

        if (slab->in_use == 0) {
            // This slab is empty - remove it from slab_list
            *prev_ptr = next;

            // Also remove from partial_list if present
            if (cache->partial_list == slab) {
                cache->partial_list = nullptr;
                // Find another partial slab
                for (Slab *s = cache->slab_list; s; s = s->next) {
                    if (s->free_list) {
                        cache->partial_list = s;
                        break;
                    }
                }
            }

            // Free the slab page back to PMM
            free_slab(slab);
            pages_reclaimed++;

            // Don't update prev_ptr since we removed current
            slab = next;
        } else {
            prev_ptr = &slab->next;
            slab = next;
        }
    }

    return pages_reclaimed;
}

/**
 * @brief Find a free cache slot.
 */
SlabCache *find_free_cache_slot() {
    for (usize i = 0; i < MAX_CACHES; i++) {
        if (!caches[i].active) {
            return &caches[i];
        }
    }
    return nullptr;
}

} // namespace

/** @copydoc slab::init */
void init() {
    serial::puts("[slab] Initializing slab allocator\n");

    SpinlockGuard guard(slab_lock);

    // Clear all cache slots
    for (usize i = 0; i < MAX_CACHES; i++) {
        caches[i].active = false;
        caches[i].slab_list = nullptr;
        caches[i].partial_list = nullptr;
        caches[i].alloc_count = 0;
        caches[i].free_count = 0;
    }

    initialized = true;
    serial::puts("[slab] Slab allocator initialized\n");
}

/** @copydoc slab::cache_create */
SlabCache *cache_create(const char *name, u32 object_size) {
    SpinlockGuard guard(slab_lock);

    if (!initialized) {
        serial::puts("[slab] ERROR: Slab allocator not initialized\n");
        return nullptr;
    }

    // Minimum object size is pointer size (for free list)
    if (object_size < sizeof(void *)) {
        object_size = sizeof(void *);
    }

    // Align to 8 bytes
    object_size = (object_size + 7) & ~7U;

    SlabCache *cache = find_free_cache_slot();
    if (!cache) {
        serial::puts("[slab] ERROR: No free cache slots\n");
        return nullptr;
    }

    // Calculate objects per slab
    // Available space = PAGE_SIZE - sizeof(Slab) header (aligned)
    u64 header_size = (sizeof(Slab) + 7) & ~7ULL;
    u64 available = pmm::PAGE_SIZE - header_size;
    u32 objects_per_slab = static_cast<u32>(available / object_size);

    if (objects_per_slab == 0) {
        serial::puts("[slab] ERROR: Object too large for slab\n");
        return nullptr;
    }

    // Initialize cache
    lib::strcpy_safe(cache->name, name, MAX_CACHE_NAME);
    cache->object_size = object_size;
    cache->objects_per_slab = objects_per_slab;
    cache->slab_list = nullptr;
    cache->partial_list = nullptr;
    cache->alloc_count = 0;
    cache->free_count = 0;
    cache->active = true;

    serial::puts("[slab] Created cache '");
    serial::puts(cache->name);
    serial::puts("' (obj_size=");
    serial::put_dec(cache->object_size);
    serial::puts(", per_slab=");
    serial::put_dec(cache->objects_per_slab);
    serial::puts(")\n");

    return cache;
}

/** @copydoc slab::cache_destroy */
void cache_destroy(SlabCache *cache) {
    if (!cache || !cache->active) {
        return;
    }

    // Acquire global lock first, then per-cache lock (lock ordering)
    SpinlockGuard global_guard(slab_lock);
    SpinlockGuard cache_guard(cache->lock);

    // Free all slabs
    Slab *slab = cache->slab_list;
    while (slab) {
        Slab *next = slab->next;
        free_slab(slab);
        slab = next;
    }

    serial::puts("[slab] Destroyed cache '");
    serial::puts(cache->name);
    serial::puts("'\n");

    cache->active = false;
    cache->slab_list = nullptr;
    cache->partial_list = nullptr;
}

/** @copydoc slab::alloc */
void *alloc(SlabCache *cache) {
    if (!cache || !cache->active) {
        return nullptr;
    }

    SpinlockGuard guard(cache->lock);

    // Try to find a slab with free objects (check partial list first)
    Slab *slab = cache->partial_list;

    if (!slab) {
        // No partial slabs - need a new slab
        slab = allocate_slab(cache);
        if (!slab) {
            return nullptr;
        }

        // Add to slab list
        slab->next = cache->slab_list;
        cache->slab_list = slab;

        // Add to partial list
        cache->partial_list = slab;
    }

    // Allocate from this slab's free list
    void *obj = slab->free_list;
    if (!obj) {
        // This shouldn't happen if partial_list is managed correctly
        serial::puts("[slab] ERROR: Slab in partial list has no free objects!\n");
        return nullptr;
    }

    // Advance free list
    slab->free_list = *reinterpret_cast<void **>(obj);
    slab->in_use++;
    cache->alloc_count++;

    // If slab is now full, remove from partial list
    if (!slab->free_list) {
        // Find and remove from partial list
        if (cache->partial_list == slab) {
            cache->partial_list = nullptr;
            // Find another partial slab
            for (Slab *s = cache->slab_list; s; s = s->next) {
                if (s->free_list) {
                    cache->partial_list = s;
                    break;
                }
            }
        }
    }

    return obj;
}

/** @copydoc slab::zalloc */
void *zalloc(SlabCache *cache) {
    void *obj = alloc(cache);
    if (obj) {
        lib::memset(obj, 0, cache->object_size);
    }
    return obj;
}

/** @copydoc slab::free */
void free(SlabCache *cache, void *ptr) {
    if (!cache || !cache->active || !ptr) {
        return;
    }

    SpinlockGuard guard(cache->lock);

    // Find the slab containing this object
    Slab *slab = find_slab_for_object(ptr);

    // O(1) ownership verification using the slab's cache pointer
    if (slab->cache != cache) {
        serial::puts("[slab] ERROR: Object does not belong to this cache!\n");
        return;
    }

    // Double-free detection: check if pointer is already in the free list
    for (void *p = slab->free_list; p != nullptr; p = *reinterpret_cast<void **>(p)) {
        if (p == ptr) {
            serial::puts("[slab] ERROR: Double-free detected! ptr=");
            serial::put_hex(reinterpret_cast<u64>(ptr));
            serial::puts(" cache=");
            serial::puts(cache->name);
            serial::puts("\n");
            return; // Don't corrupt the free list
        }
    }

    // Was this slab previously full?
    bool was_full = (slab->free_list == nullptr);

    // Add to slab's free list
    *reinterpret_cast<void **>(ptr) = slab->free_list;
    slab->free_list = ptr;
    slab->in_use--;
    cache->free_count++;

    // If slab was full, add to partial list
    if (was_full) {
        // Just set as partial list head for simplicity
        // (could be smarter about ordering)
        cache->partial_list = slab;
    }

    // Note: We don't free empty slabs back to PMM for now.
    // This keeps the slabs "hot" for future allocations.
    // A more sophisticated implementation could have a reap function.
}

/** @copydoc slab::cache_stats */
void cache_stats(SlabCache *cache, u32 *out_slabs, u32 *out_objects_used, u32 *out_objects_total) {
    if (!cache || !cache->active) {
        if (out_slabs)
            *out_slabs = 0;
        if (out_objects_used)
            *out_objects_used = 0;
        if (out_objects_total)
            *out_objects_total = 0;
        return;
    }

    SpinlockGuard guard(cache->lock);

    u32 slabs = 0;
    u32 used = 0;
    u32 total = 0;

    for (Slab *s = cache->slab_list; s; s = s->next) {
        slabs++;
        used += s->in_use;
        total += s->total;
    }

    if (out_slabs)
        *out_slabs = slabs;
    if (out_objects_used)
        *out_objects_used = used;
    if (out_objects_total)
        *out_objects_total = total;
}

/** @copydoc slab::dump_stats */
void dump_stats() {
    serial::puts("[slab] === Slab Allocator Statistics ===\n");

    // Global lock to iterate cache table, per-cache lock for stats
    SpinlockGuard global_guard(slab_lock);

    for (usize i = 0; i < MAX_CACHES; i++) {
        if (caches[i].active) {
            SlabCache *cache = &caches[i];
            SpinlockGuard cache_guard(cache->lock);

            u32 slabs = 0;
            u32 used = 0;
            u32 total = 0;
            for (Slab *s = cache->slab_list; s; s = s->next) {
                slabs++;
                used += s->in_use;
                total += s->total;
            }

            serial::puts("  ");
            serial::puts(cache->name);
            serial::puts(": obj_size=");
            serial::put_dec(cache->object_size);
            serial::puts(" slabs=");
            serial::put_dec(slabs);
            serial::puts(" used=");
            serial::put_dec(used);
            serial::puts("/");
            serial::put_dec(total);
            serial::puts(" allocs=");
            serial::put_dec(cache->alloc_count);
            serial::puts(" frees=");
            serial::put_dec(cache->free_count);
            serial::puts("\n");
        }
    }
}

/** @copydoc slab::inode_cache */
SlabCache *inode_cache() {
    return g_inode_cache;
}

/** @copydoc slab::task_cache */
SlabCache *task_cache() {
    return g_task_cache;
}

/** @copydoc slab::viper_cache */
SlabCache *viper_cache() {
    return g_viper_cache;
}

/** @copydoc slab::channel_cache */
SlabCache *channel_cache() {
    return g_channel_cache;
}

/** @copydoc slab::init_object_caches */
void init_object_caches() {
    serial::puts("[slab] Creating standard object caches\n");

    // Inode cache - 256 bytes per object
    g_inode_cache = cache_create("inode", 256);
    if (!g_inode_cache) {
        serial::puts("[slab] WARNING: Failed to create inode cache\n");
    }

    // Task cache - 1024 bytes per object (Task struct is ~900 bytes)
    g_task_cache = cache_create("task", 1024);
    if (!g_task_cache) {
        serial::puts("[slab] WARNING: Failed to create task cache\n");
    }

    // Viper cache - 512 bytes per object (Viper struct is ~450 bytes)
    g_viper_cache = cache_create("viper", 512);
    if (!g_viper_cache) {
        serial::puts("[slab] WARNING: Failed to create viper cache\n");
    }

    // Channel cache - 32 bytes per object (kobj::Channel is small)
    g_channel_cache = cache_create("channel", 32);
    if (!g_channel_cache) {
        serial::puts("[slab] WARNING: Failed to create channel cache\n");
    }

    serial::puts("[slab] Standard object caches created\n");
}

/** @copydoc slab::cache_reap */
u64 cache_reap(SlabCache *cache) {
    if (!cache || !cache->active) {
        return 0;
    }

    SpinlockGuard guard(cache->lock);

    u64 pages_reclaimed = reap_slab_list(cache);

    if (pages_reclaimed > 0) {
        serial::puts("[slab] Reaped ");
        serial::put_dec(pages_reclaimed);
        serial::puts(" pages from cache '");
        serial::puts(cache->name);
        serial::puts("'\n");
    }

    return pages_reclaimed;
}

/** @copydoc slab::reap */
u64 reap() {
    serial::puts("[slab] Starting slab reap...\n");

    u64 total_reclaimed = 0;

    // Global lock to iterate cache table
    SpinlockGuard global_guard(slab_lock);

    for (usize i = 0; i < MAX_CACHES; i++) {
        if (caches[i].active) {
            SlabCache *cache = &caches[i];
            SpinlockGuard cache_guard(cache->lock);

            total_reclaimed += reap_slab_list(cache);
        }
    }

    serial::puts("[slab] Reap complete: ");
    serial::put_dec(total_reclaimed);
    serial::puts(" pages reclaimed\n");

    return total_reclaimed;
}

} // namespace slab
