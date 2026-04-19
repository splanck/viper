//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_pool.c
// Purpose: Implements a slab allocator for the Viper runtime. Reduces
//          malloc/free overhead by pooling fixed-size allocations into
//          four size classes (64, 128, 256, 512 bytes) and reusing freed
//          blocks via lock-free intrusive freelists.
//
// Key invariants:
//   - Each size class maintains a singly-linked list of slabs; each slab holds
//     BLOCKS_PER_SLAB (64) fixed-size blocks.
//   - Free blocks are tracked via tagged pointers that embed a 16-bit version
//     counter in the upper bits to prevent ABA races on CAS operations.
//   - Slab list insertion uses atomic CAS; no mutex is held during allocation.
//   - Allocation requests larger than the largest size class (512 bytes) fall
//     through to the system allocator.
//   - Blocks are zeroed on both allocation and recycling (memset in alloc/free).
//
// Ownership/Lifetime:
//   - Slabs are allocated from the system heap and never freed individually;
//     they persist until the process exits.
//   - Freed blocks are returned to the per-class freelist and owned by the pool
//     until the next allocation of the same class.
//
// Links: src/runtime/core/rt_pool.h (public API),
//        src/runtime/core/rt_heap.c (heap layer above pool),
//        src/runtime/core/rt_memory.c (low-level allocation primitives)
//
//===----------------------------------------------------------------------===//

#include "rt_pool.h"
#include "rt_platform.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#if !RT_PLATFORM_WINDOWS && !RT_PLATFORM_VIPERDOS
#include <sched.h>
#endif

/// @brief Number of blocks per slab in each size class.
/// Tuned for balance between memory efficiency and allocation frequency.
#define BLOCKS_PER_SLAB 64

/// @brief Size of each size class in bytes.
static const size_t kClassSizes[RT_POOL_COUNT] = {64, 128, 256, 512};

/// @brief Header for each block on the freelist.
/// Uses intrusive linking - the header occupies the first bytes of the block.
/// The `next` pointer MUST be accessed via atomic_load_next / atomic_store_next
/// helpers because concurrent threads may read/write it through the lock-free
/// freelist (push_to_freelist writes `next`, pop_from_freelist reads it).
typedef struct rt_pool_block {
    struct rt_pool_block *next;
} rt_pool_block_t;

/// @brief Atomically load a block's next pointer (acquire).
static inline rt_pool_block_t *atomic_load_next(rt_pool_block_t *block) {
#if RT_COMPILER_MSVC
    rt_pool_block_t *val = *(volatile rt_pool_block_t **)&block->next;
#if defined(_M_ARM64)
    __dmb(_ARM64_BARRIER_ISH);
#else
    _ReadWriteBarrier();
#endif
    return val;
#else
    return __atomic_load_n(&block->next, __ATOMIC_ACQUIRE);
#endif
}

/// @brief Atomically store a block's next pointer (release).
static inline void atomic_store_next(rt_pool_block_t *block, rt_pool_block_t *next) {
#if RT_COMPILER_MSVC
#if defined(_M_ARM64)
    __dmb(_ARM64_BARRIER_ISH);
#else
    _ReadWriteBarrier();
#endif
    *(volatile rt_pool_block_t **)&block->next = next;
#else
    __atomic_store_n(&block->next, next, __ATOMIC_RELEASE);
#endif
}

/// @brief Slab metadata - tracks a single large allocation subdivided into blocks.
typedef struct rt_pool_slab {
    struct rt_pool_slab *next; ///< Next slab in the size class
    size_t block_size;         ///< Size of each block in this slab
    size_t block_count;        ///< Number of blocks in this slab
    char *data;                ///< Start of block data
} rt_pool_slab_t;

//===----------------------------------------------------------------------===//
// Tagged Pointer Support (ABA Prevention)
//===----------------------------------------------------------------------===//
//
// Tagged pointers use the upper 16 bits for a version counter and the lower
// 48 bits for the actual pointer. This works because:
// - x86-64 uses only 48 bits for user-space virtual addresses
// - Pool blocks are aligned to at least 8 bytes
// - The version counter detects ABA scenarios where a pointer is recycled
//
// On Pointer-Authentication targets we avoid tagged pointers entirely and fall
// back to a lock-protected raw-pointer freelist so PAC bits remain intact.
//
//===----------------------------------------------------------------------===//

#if defined(__arm64e__) || defined(_M_ARM64EC)
#define RT_POOL_PAC_SAFE 1
#elif defined(__has_feature)
#if __has_feature(ptrauth_calls)
#define RT_POOL_PAC_SAFE 1
#else
#define RT_POOL_PAC_SAFE 0
#endif
#else
#define RT_POOL_PAC_SAFE 0
#endif

/// @brief Pack a pointer and version into a tagged pointer.
#if !RT_POOL_PAC_SAFE
static inline uint64_t pack_tagged_ptr(void *ptr, uint16_t version) {
    return ((uint64_t)version << 48) | ((uint64_t)(uintptr_t)ptr & 0x0000FFFFFFFFFFFFULL);
}

/// @brief Extract the pointer from a tagged pointer.
static inline void *unpack_ptr(uint64_t tagged) {
    return (void *)(uintptr_t)(tagged & 0x0000FFFFFFFFFFFFULL);
}

/// @brief Extract the version from a tagged pointer.
static inline uint16_t unpack_version(uint64_t tagged) {
    return (uint16_t)(tagged >> 48);
}
#endif

/// @brief Atomic compare-exchange for 64-bit values.
static inline int atomic_cas_u64(volatile uint64_t *ptr, uint64_t *expected, uint64_t desired) {
#if RT_COMPILER_MSVC
    uint64_t old = _InterlockedCompareExchange64(
        (volatile long long *)ptr, (long long)desired, (long long)*expected);
    if (old == *expected)
        return 1;
    *expected = old;
    return 0;
#else
    return __atomic_compare_exchange_n(
        ptr, expected, desired, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
}

/// @brief Atomic load for 64-bit values.
/// CONC-004 fix: ARM64 Windows uses __dmb for CPU barrier instead of
/// compiler-only _ReadWriteBarrier() which is insufficient on weak memory models.
static inline uint64_t atomic_load_u64(volatile uint64_t *ptr) {
#if RT_COMPILER_MSVC
#if defined(_M_ARM64)
    uint64_t value = *ptr;
    __dmb(_ARM64_BARRIER_ISH); /* acquire: CPU load fence */
    return value;
#elif defined(_M_X64)
    uint64_t value = *ptr;
    _ReadWriteBarrier(); /* x86-64 TSO: compiler fence suffices */
    return value;
#else
    return (uint64_t)_InterlockedCompareExchange64((volatile long long *)ptr, 0, 0);
#endif
#else
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
#endif
}

/// @brief Atomic store for 64-bit values.
/// CONC-004 fix: ARM64 Windows uses __dmb for CPU barrier.
static inline void atomic_store_u64(volatile uint64_t *ptr, uint64_t value) {
#if RT_COMPILER_MSVC
#if defined(_M_ARM64)
    __dmb(_ARM64_BARRIER_ISH); /* release: CPU store fence */
    *ptr = value;
    __dmb(_ARM64_BARRIER_ISH);
#elif defined(_M_X64)
    _ReadWriteBarrier();
    *ptr = value;
    _ReadWriteBarrier();
#else
    _InterlockedExchange64((volatile long long *)ptr, (long long)value);
#endif
#else
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
#endif
}

/// @brief Per-size-class pool state.
/// CONC-005 fix: volatile qualifiers removed — all accesses use __atomic_*
/// builtins or CAS operations which provide their own compiler+CPU barriers.
typedef struct rt_pool_state {
#if RT_POOL_PAC_SAFE
    rt_pool_block_t *freelist_head; ///< PAC-safe raw-pointer freelist head.
    int freelist_lock;              ///< Spinlock protecting PAC-safe freelist operations.
#else
    uint64_t freelist_tagged; ///< Lock-free freelist head (tagged pointer, via atomic CAS)
#endif
    rt_pool_slab_t *slabs; ///< List of slabs (via atomic CAS for thread-safe insertion)
    size_t allocated;      ///< Count of blocks currently allocated (via __atomic_*)
    size_t free_count;     ///< Count of blocks on freelist (via __atomic_*)
} rt_pool_state_t;

/// @brief Global pool state for each size class.
static rt_pool_state_t g_pools[RT_POOL_COUNT];

#if RT_POOL_PAC_SAFE
/// @brief Acquire the PAC-safe freelist spinlock with yield-on-contention.
/// @details Used only on Pointer-Authentication targets (arm64e on
///          Apple Silicon) where tagged pointers would corrupt PAC
///          signatures. Falls back to a simple lock-protected
///          freelist instead of the lock-free tagged-pointer CAS.
static void rt_pool_lock_(int *lock) {
    if (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
        do {
#if RT_PLATFORM_WINDOWS
            SwitchToThread();
#elif !RT_PLATFORM_VIPERDOS
            sched_yield();
#endif
        } while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE));
    }
}

/// @brief Release the PAC-safe freelist spinlock with release semantics.
static void rt_pool_unlock_(int *lock) {
    __atomic_clear(lock, __ATOMIC_RELEASE);
}
#endif

/// @brief Determine the size class for a given allocation size.
/// @param size Requested allocation size.
/// @return Size class index, or RT_POOL_COUNT if size exceeds max.
static rt_pool_class_t size_to_class(size_t size) {
    if (size <= 64)
        return RT_POOL_64;
    if (size <= 128)
        return RT_POOL_128;
    if (size <= 256)
        return RT_POOL_256;
    if (size <= 512)
        return RT_POOL_512;
    return RT_POOL_COUNT; // Too large for pooling
}

/// @brief Allocate a new slab for the given size class.
/// @param class_idx Size class index.
/// @return New slab, or NULL on allocation failure.
static rt_pool_slab_t *allocate_slab(rt_pool_class_t class_idx) {
    size_t block_size = kClassSizes[class_idx];
    size_t data_size = block_size * BLOCKS_PER_SLAB;

    // Allocate slab metadata and data together
    rt_pool_slab_t *slab = (rt_pool_slab_t *)malloc(sizeof(rt_pool_slab_t) + data_size);
    if (!slab)
        return NULL;

    slab->next = NULL;
    slab->block_size = block_size;
    slab->block_count = BLOCKS_PER_SLAB;
    slab->data = (char *)(slab + 1);

    // Zero-initialize all blocks
    memset(slab->data, 0, data_size);

    return slab;
}

/// @brief Pop a block from the freelist.
/// @param pool Pool state for the size class.
/// @return Block pointer, or NULL if freelist is empty.
/// @note Uses tagged pointers to prevent ABA problems. The version counter
///       in the upper 16 bits ensures that even if a block is recycled back
///       to the same address, the CAS will fail due to version mismatch.
static rt_pool_block_t *pop_from_freelist(rt_pool_state_t *pool) {
#if RT_POOL_PAC_SAFE
    rt_pool_lock_(&pool->freelist_lock);
    rt_pool_block_t *head = pool->freelist_head;
    if (head)
        pool->freelist_head = head->next;
    rt_pool_unlock_(&pool->freelist_lock);
    if (!head)
        return NULL;
#if RT_COMPILER_MSVC
    rt_atomic_fetch_sub_size(&pool->free_count, 1, __ATOMIC_RELAXED);
#else
    __atomic_fetch_sub(&pool->free_count, 1, __ATOMIC_RELAXED);
#endif
    return head;
#else
    uint64_t old_tagged = atomic_load_u64(&pool->freelist_tagged);

    while (1) {
        rt_pool_block_t *head = (rt_pool_block_t *)unpack_ptr(old_tagged);
        if (!head)
            return NULL;

        uint16_t old_version = unpack_version(old_tagged);
        rt_pool_block_t *next = atomic_load_next(head);

        // Pack the new tagged pointer with incremented version
        uint64_t new_tagged = pack_tagged_ptr(next, (uint16_t)(old_version + 1));

        if (atomic_cas_u64(&pool->freelist_tagged, &old_tagged, new_tagged)) {
#if RT_COMPILER_MSVC
            rt_atomic_fetch_sub_size(&pool->free_count, 1, __ATOMIC_RELAXED);
#else
            __atomic_fetch_sub(&pool->free_count, 1, __ATOMIC_RELAXED);
#endif
            return head;
        }
        // CAS failed, old_tagged was updated with the current value - retry
    }
#endif
}

/// @brief Push a block back onto the freelist.
/// @param pool Pool state for the size class.
/// @param block Block to return to the freelist.
static void push_to_freelist(rt_pool_state_t *pool, rt_pool_block_t *block) {
#if RT_POOL_PAC_SAFE
    rt_pool_lock_(&pool->freelist_lock);
    atomic_store_next(block, pool->freelist_head);
    pool->freelist_head = block;
    rt_pool_unlock_(&pool->freelist_lock);
#else
    uint64_t old_tagged = atomic_load_u64(&pool->freelist_tagged);
    uint64_t new_tagged;
    do {
        rt_pool_block_t *old_head = (rt_pool_block_t *)unpack_ptr(old_tagged);
        uint16_t old_version = unpack_version(old_tagged);
        atomic_store_next(block, old_head);
        new_tagged = pack_tagged_ptr(block, (uint16_t)(old_version + 1));
    } while (!atomic_cas_u64(&pool->freelist_tagged, &old_tagged, new_tagged));
#endif

#if RT_COMPILER_MSVC
    rt_atomic_fetch_add_size(&pool->free_count, 1, __ATOMIC_RELAXED);
#else
    __atomic_fetch_add(&pool->free_count, 1, __ATOMIC_RELAXED);
#endif
}

/// @brief Allocate a zeroed block from the pool or fall back to malloc.
/// @details Selects the smallest size class that fits @p size, pops a block from
///          the class freelist if available, or allocates a fresh slab of 64 blocks
///          and returns the first one (pushing the rest onto the freelist for future use).
///          Large allocations (> 512 bytes) bypass the pool entirely and use malloc.
/// @param size Number of bytes requested; rounded up to the enclosing size class.
/// @return Pointer to zeroed memory, or NULL on failure.
void *rt_pool_alloc(size_t size) {
    if (size == 0)
        size = 1; // Minimum allocation

    rt_pool_class_t class_idx = size_to_class(size);

    // Fall back to malloc for large allocations
    if (class_idx >= RT_POOL_COUNT)
        return malloc(size);

    rt_pool_state_t *pool = &g_pools[class_idx];

    // Try to pop from freelist
    rt_pool_block_t *block = pop_from_freelist(pool);

    if (!block) {
        // Freelist empty - allocate a new slab
        rt_pool_slab_t *slab = allocate_slab(class_idx);
        if (!slab)
            return NULL;

        // Reserve the first block for this allocation before pushing the
        // rest to the freelist. This prevents a race where other threads
        // consume all blocks between push and our pop, causing a spurious
        // NULL return.
        block = (rt_pool_block_t *)(slab->data);

        // Atomically link slab into list using CAS loop
        // This prevents the lost-update race where concurrent slab allocations
        // could orphan one or more slabs (RACE-002 fix)
#if RT_COMPILER_MSVC
        rt_pool_slab_t *expected = (rt_pool_slab_t *)rt_atomic_load_ptr(
            (void *const volatile *)&pool->slabs, __ATOMIC_RELAXED);
        do {
            slab->next = expected;
        } while (!rt_atomic_compare_exchange_ptr((void *volatile *)&pool->slabs,
                                                 (void **)&expected,
                                                 slab,
                                                 __ATOMIC_RELEASE,
                                                 __ATOMIC_RELAXED));
#else
        rt_pool_slab_t *expected = __atomic_load_n(&pool->slabs, __ATOMIC_RELAXED);
        do {
            slab->next = expected;
        } while (!__atomic_compare_exchange_n(
            &pool->slabs, &expected, slab, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
#endif

        // Push remaining blocks (skip first, which is reserved) to freelist
        if (slab->block_count > 1) {
            rt_pool_block_t *first = NULL;
            rt_pool_block_t *last = NULL;

            for (size_t i = 1; i < slab->block_count; i++) {
                rt_pool_block_t *b = (rt_pool_block_t *)(slab->data + i * slab->block_size);
                b->next = NULL;

                if (!first) {
                    first = b;
                    last = b;
                } else {
                    last->next = b;
                    last = b;
                }
            }

#if RT_POOL_PAC_SAFE
            rt_pool_lock_(&pool->freelist_lock);
            atomic_store_next(last, pool->freelist_head);
            pool->freelist_head = first;
            rt_pool_unlock_(&pool->freelist_lock);
#else
            // Atomically prepend the chain to the freelist
            uint64_t old_tagged = atomic_load_u64(&pool->freelist_tagged);
            uint64_t new_tagged;
            do {
                rt_pool_block_t *old_head = (rt_pool_block_t *)unpack_ptr(old_tagged);
                uint16_t old_version = unpack_version(old_tagged);
                atomic_store_next(last, old_head);
                new_tagged = pack_tagged_ptr(first, (uint16_t)(old_version + 1));
            } while (!atomic_cas_u64(&pool->freelist_tagged, &old_tagged, new_tagged));
#endif

#if RT_COMPILER_MSVC
            rt_atomic_fetch_add_size(&pool->free_count, slab->block_count - 1, __ATOMIC_RELAXED);
#else
            __atomic_fetch_add(&pool->free_count, slab->block_count - 1, __ATOMIC_RELAXED);
#endif
        }
    }

#if RT_COMPILER_MSVC
    rt_atomic_fetch_add_size(&pool->allocated, 1, __ATOMIC_RELAXED);
#else
    __atomic_fetch_add(&pool->allocated, 1, __ATOMIC_RELAXED);
#endif

    // Zero the block before returning (caller expects zeroed memory)
    memset(block, 0, kClassSizes[class_idx]);

    return block;
}

/// @brief Return a block to the pool freelist, or free via the system allocator.
/// @details Zeros the block for security and debugging (prevents data leaks from
///          recycled memory), then pushes it back onto the size class freelist.
///          Large allocations that bypassed the pool are freed via free().
/// @param ptr Pointer previously returned by rt_pool_alloc; NULL is a no-op.
/// @param size Original allocation size (determines which size class to return to).
void rt_pool_free(void *ptr, size_t size) {
    if (!ptr)
        return;

    rt_pool_class_t class_idx = size_to_class(size);

    // Large allocations were from malloc
    if (class_idx >= RT_POOL_COUNT) {
        free(ptr);
        return;
    }

    rt_pool_state_t *pool = &g_pools[class_idx];

    // Clear the block before returning to pool (security/debugging)
    memset(ptr, 0, kClassSizes[class_idx]);

    // Push block back to freelist
    push_to_freelist(pool, (rt_pool_block_t *)ptr);

#if RT_COMPILER_MSVC
    rt_atomic_fetch_sub_size(&pool->allocated, 1, __ATOMIC_RELAXED);
#else
    __atomic_fetch_sub(&pool->allocated, 1, __ATOMIC_RELAXED);
#endif
}

/// @brief Query per-class allocation statistics for monitoring and diagnostics.
/// @param class_idx Size class to query (0 = 64B, 1 = 128B, 2 = 256B, 3 = 512B).
/// @param out_allocated Receives the number of blocks currently in use.
/// @param out_free Receives the number of blocks sitting on the freelist.
void rt_pool_stats(rt_pool_class_t class_idx, size_t *out_allocated, size_t *out_free) {
    if (class_idx >= RT_POOL_COUNT) {
        if (out_allocated)
            *out_allocated = 0;
        if (out_free)
            *out_free = 0;
        return;
    }

    rt_pool_state_t *pool = &g_pools[class_idx];

#if RT_COMPILER_MSVC
    if (out_allocated)
        *out_allocated = rt_atomic_load_size(&pool->allocated, __ATOMIC_RELAXED);
    if (out_free)
        *out_free = rt_atomic_load_size(&pool->free_count, __ATOMIC_RELAXED);
#else
    if (out_allocated)
        *out_allocated = __atomic_load_n(&pool->allocated, __ATOMIC_RELAXED);
    if (out_free)
        *out_free = __atomic_load_n(&pool->free_count, __ATOMIC_RELAXED);
#endif
}

/// @brief Release all slab memory back to the system allocator.
/// @details Walks every slab in every size class and frees it. Resets all pool
///          state (freelist, counts) to zero. Called only during process shutdown
///          after rt_gc_run_all_finalizers and rt_string_intern_drain have already
///          released all live references — calling earlier causes use-after-free.
void rt_pool_shutdown(void) {
    for (int i = 0; i < RT_POOL_COUNT; i++) {
        rt_pool_state_t *pool = &g_pools[i];

        // Free all slabs
        rt_pool_slab_t *slab = pool->slabs;
        while (slab) {
            rt_pool_slab_t *next = slab->next;
            free(slab);
            slab = next;
        }

        // Reset state
        pool->slabs = NULL;
#if RT_POOL_PAC_SAFE
        pool->freelist_head = NULL;
        __atomic_clear(&pool->freelist_lock, __ATOMIC_RELEASE);
#else
        atomic_store_u64(&pool->freelist_tagged, 0);
#endif
#if RT_COMPILER_MSVC
        rt_atomic_store_size(&pool->allocated, 0, __ATOMIC_RELAXED);
        rt_atomic_store_size(&pool->free_count, 0, __ATOMIC_RELAXED);
#else
        __atomic_store_n(&pool->allocated, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&pool->free_count, 0, __ATOMIC_RELAXED);
#endif
    }
}
