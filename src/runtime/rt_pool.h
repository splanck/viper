//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares a slab allocator for the Viper runtime. The pool allocator
// reduces allocation overhead by pre-allocating blocks in size classes and reusing
// freed blocks without returning to the system allocator.
//
// Size Classes:
// - 64 bytes (for allocations 1-64 bytes)
// - 128 bytes (for allocations 65-128 bytes)
// - 256 bytes (for allocations 129-256 bytes)
// - 512 bytes (for allocations 257-512 bytes)
//
// Allocations larger than 512 bytes fall back to malloc/free.
//
// Thread Safety:
// The pool allocator uses atomic operations for thread-safe freelist management.
// Multiple threads can allocate and free concurrently without external locking.
//
// Performance:
// - Allocation: O(1) from freelist, O(slab_size) when allocating new slab
// - Deallocation: O(1) push to freelist
// - Memory overhead: ~1-2% for block headers and slab metadata
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief Size classes for the pool allocator.
typedef enum
{
    RT_POOL_64 = 0,  ///< 64-byte blocks
    RT_POOL_128 = 1, ///< 128-byte blocks
    RT_POOL_256 = 2, ///< 256-byte blocks
    RT_POOL_512 = 3, ///< 512-byte blocks
    RT_POOL_COUNT = 4
} rt_pool_class_t;

/// @brief Maximum size handled by the pool allocator.
#define RT_POOL_MAX_SIZE 512

/// @brief Allocate memory from the pool.
/// @details Allocates from the appropriate size class pool. Falls back to
///          malloc for sizes > RT_POOL_MAX_SIZE.
/// @param size Number of bytes to allocate.
/// @return Pointer to allocated memory, or NULL on failure.
void *rt_pool_alloc(size_t size);

/// @brief Free memory back to the pool.
/// @details Returns the block to its size class freelist. For large allocations
///          (> RT_POOL_MAX_SIZE), delegates to free().
/// @param ptr Pointer to memory previously allocated by rt_pool_alloc.
/// @param size Original allocation size (used to determine size class).
void rt_pool_free(void *ptr, size_t size);

/// @brief Get statistics about pool usage.
/// @details Returns the number of allocated and free blocks in each size class.
/// @param class_idx Size class index (0-3).
/// @param out_allocated Output: number of blocks currently allocated.
/// @param out_free Output: number of blocks on freelist.
void rt_pool_stats(rt_pool_class_t class_idx, size_t *out_allocated, size_t *out_free);

/// @brief Release all pool memory back to the system.
/// @details Frees all slabs in all size classes. Should only be called during
///          program shutdown when all pool allocations have been freed.
/// @warning Calling this while allocations are still in use causes undefined behavior.
void rt_pool_shutdown(void);

#ifdef __cplusplus
}
#endif
