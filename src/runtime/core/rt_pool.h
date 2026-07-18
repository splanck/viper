//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_pool.h
// Purpose: Slab allocator with four size classes (64, 128, 256, 512 bytes), reducing allocation
// overhead by reusing freed blocks without returning to the system allocator.
//
// Key invariants:
//   - Size classes cover 1-64, 65-128, 129-256, and 257-512 byte allocations.
//   - Allocations larger than 512 bytes fall back to malloc/free.
//   - Freelist management is synchronized; multiple threads may allocate concurrently.
//   - Private per-block metadata routes frees in O(1) and detects duplicate release.
//   - Freed blocks stay in the freelist until shutdown reclaims a quiescent class.
//   - Shutdown never reclaims a class with outstanding caller-owned blocks.
//   - Ordinary operations use an atomic lifecycle epoch rather than one global hot-path lock.
//
// Ownership/Lifetime:
//   - Callers receive a pointer to the allocated block; no header is exposed.
//   - rt_pool_free accepts the original requested size. Small blocks carry a
//     private aligned header that validates and identifies their owning slab.
//   - The pool is process-global and supports an explicit shutdown path.
//
// Links: src/runtime/core/rt_pool.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Size classes for the pool allocator.
typedef enum {
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
/// @details Waits for concurrent shutdown coordination, then returns the block
///          to its owning size-class freelist in O(1) through private metadata.
///          A duplicate small-block release traps without modifying the
///          freelist or statistics. For large allocations, delegates to free().
/// @param ptr Pointer to memory previously allocated by rt_pool_alloc.
/// @param size Original allocation size, used to select the expected fast path.
void rt_pool_free(void *ptr, size_t size);

/// @brief Get statistics about pool usage.
/// @details Returns the number of allocated and free blocks in each size class.
/// @param class_idx Size class index (0-3).
/// @param out_allocated Output: number of blocks currently allocated.
/// @param out_free Output: number of blocks on freelist.
void rt_pool_stats(rt_pool_class_t class_idx, size_t *out_allocated, size_t *out_free);

/// @brief Release all pool memory back to the system.
/// @details Stops new slab-backed operations, waits for in-flight pool operations
///          to leave their critical sections, and frees slabs only for classes
///          with no outstanding blocks. Live classes remain valid and are
///          reclaimed by a later shutdown after their final release. A later
///          allocation can rebuild classes that were reclaimed.
void rt_pool_shutdown(void);

#ifdef __cplusplus
}
#endif
