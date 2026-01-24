//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/mm/slab.hpp
// Purpose: Slab allocator for efficient fixed-size object allocation.
// Key invariants: O(1) alloc/free; objects packed in 4KB slabs.
// Ownership/Lifetime: Global caches; init after PMM.
// Links: kernel/mm/slab.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"
#include "../lib/spinlock.hpp"

/**
 * @file slab.hpp
 * @brief Slab allocator for efficient fixed-size object allocation.
 *
 * @details
 * The slab allocator provides O(1) allocation and deallocation for fixed-size
 * objects. Each "slab cache" manages objects of a specific size, carved from
 * 4KB pages obtained from the PMM.
 *
 * Benefits over the general-purpose heap:
 * - O(1) alloc/free (no free-list traversal)
 * - Better cache locality (objects of same type packed together)
 * - Zero fragmentation within a cache
 * - Efficient memory utilization for small objects
 *
 * Usage:
 * @code
 * // Create a cache for 256-byte objects
 * SlabCache *cache = slab::cache_create("inode", 256);
 *
 * // Allocate an object
 * void *obj = slab::alloc(cache);
 *
 * // Free the object
 * slab::free(cache, obj);
 * @endcode
 */
namespace slab {

// Forward declaration for Slab struct
struct SlabCache;

/** @brief Maximum name length for a slab cache. */
constexpr usize MAX_CACHE_NAME = 32;

/** @brief Maximum number of slab caches that can be created. */
constexpr usize MAX_CACHES = 16;

/**
 * @brief Header for a slab page.
 *
 * @details
 * Each slab is a 4KB page containing a small header followed by object slots.
 * The header maintains a free list of objects within this slab and links to
 * other slabs in the same cache.
 */
struct Slab {
    Slab *next;       ///< Next slab in the cache's slab list
    SlabCache *cache; ///< Owning cache (for O(1) ownership verification)
    void *free_list;  ///< Head of free object list within this slab
    u32 in_use;       ///< Number of objects currently allocated
    u32 total;        ///< Total number of objects in this slab
};

/**
 * @brief A slab cache managing fixed-size objects.
 *
 * @details
 * Each cache manages objects of a single size. When the cache runs out of
 * free objects, it allocates a new slab (4KB page) from the PMM.
 */
struct SlabCache {
    char name[MAX_CACHE_NAME]; ///< Cache name for debugging
    u32 object_size;           ///< Size of each object in bytes
    u32 objects_per_slab;      ///< Number of objects per 4KB slab
    Slab *slab_list;           ///< List of all slabs in this cache
    Slab *partial_list;        ///< Slabs with free objects (fast path)
    u64 alloc_count;           ///< Total allocations (statistics)
    u64 free_count;            ///< Total frees (statistics)
    bool active;               ///< Whether this cache slot is in use
    mutable Spinlock lock;     ///< Per-cache lock for SMP scalability
};

/**
 * @brief Initialize the slab allocator subsystem.
 *
 * @details
 * Clears the cache table. Should be called after PMM initialization.
 */
void init();

/**
 * @brief Create a new slab cache for fixed-size objects.
 *
 * @details
 * Creates a cache that will allocate objects of the specified size.
 * The object size is rounded up to 8-byte alignment minimum, and must be
 * large enough to hold a free-list pointer (8 bytes).
 *
 * @param name Human-readable name for debugging (max 31 chars).
 * @param object_size Size of each object in bytes.
 * @return Pointer to the cache, or nullptr on failure.
 */
SlabCache *cache_create(const char *name, u32 object_size);

/**
 * @brief Destroy a slab cache and free all its memory.
 *
 * @details
 * Returns all slabs to the PMM. Any allocated objects become invalid.
 * Only call when all objects have been freed.
 *
 * @param cache The cache to destroy.
 */
void cache_destroy(SlabCache *cache);

/**
 * @brief Allocate an object from a slab cache.
 *
 * @details
 * Returns a pointer to an uninitialized object of cache->object_size bytes.
 * If no free objects are available, allocates a new slab from the PMM.
 *
 * @param cache The slab cache to allocate from.
 * @return Pointer to the allocated object, or nullptr on failure.
 */
void *alloc(SlabCache *cache);

/**
 * @brief Allocate a zero-initialized object from a slab cache.
 *
 * @param cache The slab cache to allocate from.
 * @return Pointer to the zero-initialized object, or nullptr on failure.
 */
void *zalloc(SlabCache *cache);

/**
 * @brief Free an object back to its slab cache.
 *
 * @details
 * Returns the object to the cache's free list. The object must have been
 * allocated from this cache.
 *
 * @param cache The slab cache the object was allocated from.
 * @param ptr Pointer to the object to free.
 */
void free(SlabCache *cache, void *ptr);

/**
 * @brief Get statistics for a slab cache.
 *
 * @param cache The cache to query.
 * @param out_slabs Number of slabs allocated.
 * @param out_objects_used Number of objects currently in use.
 * @param out_objects_total Total object capacity.
 */
void cache_stats(SlabCache *cache, u32 *out_slabs, u32 *out_objects_used, u32 *out_objects_total);

/**
 * @brief Print slab allocator statistics to serial console.
 */
void dump_stats();

// ============================================================================
// Pre-defined caches for common kernel objects
// ============================================================================

/**
 * @brief Get the inode slab cache (256 bytes per object).
 * @return Pointer to the inode cache.
 */
SlabCache *inode_cache();

/**
 * @brief Initialize the pre-defined object caches.
 *
 * @details
 * Creates caches for common kernel objects. Called during kernel init.
 */
void init_object_caches();

} // namespace slab
