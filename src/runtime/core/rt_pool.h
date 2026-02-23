//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_pool.h
// Purpose: Slab allocator with four size classes (64, 128, 256, 512 bytes), reducing allocation overhead by reusing freed blocks without returning to the system allocator.
//
// Key invariants:
//   - Size classes cover 1-64, 65-128, 129-256, and 257-512 byte allocations.
//   - Allocations larger than 512 bytes fall back to malloc/free.
//   - Freelist management uses atomic operations; multiple threads may allocate concurrently.
//   - Pool memory is never returned to the OS; freed blocks stay in the freelist.
//
// Ownership/Lifetime:
//   - Callers receive a pointer to the allocated block; no header is exposed.
//   - rt_pool_free must be called with the same size class as rt_pool_alloc.
//   - The pool is process-global; it is never explicitly destroyed.
//
// Links: src/runtime/core/rt_pool.c (implementation)
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
