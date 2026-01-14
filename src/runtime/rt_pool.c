//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a slab allocator for the Viper runtime. This allocator reduces
// malloc/free overhead by pooling allocations into size classes and reusing
// freed blocks via freelists.
//
// Architecture:
// - Each size class maintains a linked list of slabs
// - Each slab is a large allocation subdivided into fixed-size blocks
// - Free blocks are tracked via an intrusive linked list (freelist)
// - Thread safety is achieved via lock-free atomic CAS on freelists
//
//===----------------------------------------------------------------------===//

#include "rt_pool.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/// @brief Number of blocks per slab in each size class.
/// Tuned for balance between memory efficiency and allocation frequency.
#define BLOCKS_PER_SLAB 64

/// @brief Size of each size class in bytes.
static const size_t kClassSizes[RT_POOL_COUNT] = {64, 128, 256, 512};

/// @brief Header for each block on the freelist.
/// Uses intrusive linking - the header occupies the first bytes of the block.
typedef struct rt_pool_block
{
    struct rt_pool_block *next;
} rt_pool_block_t;

/// @brief Slab metadata - tracks a single large allocation subdivided into blocks.
typedef struct rt_pool_slab
{
    struct rt_pool_slab *next; ///< Next slab in the size class
    size_t block_size;         ///< Size of each block in this slab
    size_t block_count;        ///< Number of blocks in this slab
    char *data;                ///< Start of block data
} rt_pool_slab_t;

/// @brief Per-size-class pool state.
typedef struct rt_pool_state
{
    _Atomic(rt_pool_block_t *) freelist; ///< Lock-free freelist head
    rt_pool_slab_t *slabs;               ///< List of slabs (not atomic - protected by allocation)
    _Atomic(size_t) allocated;           ///< Count of blocks currently allocated
    _Atomic(size_t) free_count;          ///< Count of blocks on freelist
} rt_pool_state_t;

/// @brief Global pool state for each size class.
static rt_pool_state_t g_pools[RT_POOL_COUNT];

/// @brief Determine the size class for a given allocation size.
/// @param size Requested allocation size.
/// @return Size class index, or RT_POOL_COUNT if size exceeds max.
static rt_pool_class_t size_to_class(size_t size)
{
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
static rt_pool_slab_t *allocate_slab(rt_pool_class_t class_idx)
{
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

/// @brief Push blocks from a new slab onto the freelist.
/// @param pool Pool state for the size class.
/// @param slab Newly allocated slab.
static void push_slab_to_freelist(rt_pool_state_t *pool, rt_pool_slab_t *slab)
{
    // Build a local chain of all blocks in the slab
    rt_pool_block_t *first = NULL;
    rt_pool_block_t *last = NULL;

    for (size_t i = 0; i < slab->block_count; i++)
    {
        rt_pool_block_t *block = (rt_pool_block_t *)(slab->data + i * slab->block_size);
        block->next = NULL;

        if (!first)
        {
            first = block;
            last = block;
        }
        else
        {
            last->next = block;
            last = block;
        }
    }

    // Atomically prepend the chain to the freelist
    rt_pool_block_t *expected = atomic_load_explicit(&pool->freelist, memory_order_relaxed);
    do
    {
        last->next = expected;
    } while (!atomic_compare_exchange_weak_explicit(
        &pool->freelist, &expected, first,
        memory_order_release, memory_order_relaxed));

    atomic_fetch_add_explicit(&pool->free_count, slab->block_count, memory_order_relaxed);
}

/// @brief Pop a block from the freelist.
/// @param pool Pool state for the size class.
/// @return Block pointer, or NULL if freelist is empty.
static rt_pool_block_t *pop_from_freelist(rt_pool_state_t *pool)
{
    rt_pool_block_t *head = atomic_load_explicit(&pool->freelist, memory_order_acquire);
    while (head)
    {
        rt_pool_block_t *next = head->next;
        if (atomic_compare_exchange_weak_explicit(
                &pool->freelist, &head, next,
                memory_order_acquire, memory_order_relaxed))
        {
            atomic_fetch_sub_explicit(&pool->free_count, 1, memory_order_relaxed);
            return head;
        }
        // CAS failed, head was updated - retry
    }
    return NULL;
}

/// @brief Push a block back onto the freelist.
/// @param pool Pool state for the size class.
/// @param block Block to return to the freelist.
static void push_to_freelist(rt_pool_state_t *pool, rt_pool_block_t *block)
{
    rt_pool_block_t *expected = atomic_load_explicit(&pool->freelist, memory_order_relaxed);
    do
    {
        block->next = expected;
    } while (!atomic_compare_exchange_weak_explicit(
        &pool->freelist, &expected, block,
        memory_order_release, memory_order_relaxed));

    atomic_fetch_add_explicit(&pool->free_count, 1, memory_order_relaxed);
}

void *rt_pool_alloc(size_t size)
{
    if (size == 0)
        size = 1; // Minimum allocation

    rt_pool_class_t class_idx = size_to_class(size);

    // Fall back to malloc for large allocations
    if (class_idx >= RT_POOL_COUNT)
        return malloc(size);

    rt_pool_state_t *pool = &g_pools[class_idx];

    // Try to pop from freelist
    rt_pool_block_t *block = pop_from_freelist(pool);

    if (!block)
    {
        // Freelist empty - allocate a new slab
        // Note: This is not fully thread-safe for slab list management,
        // but races just result in extra slabs (acceptable waste).
        rt_pool_slab_t *slab = allocate_slab(class_idx);
        if (!slab)
            return NULL;

        // Link slab into list (benign race - may lose slabs on shutdown)
        slab->next = pool->slabs;
        pool->slabs = slab;

        // Push all blocks to freelist
        push_slab_to_freelist(pool, slab);

        // Now pop one for this allocation
        block = pop_from_freelist(pool);
        if (!block)
            return NULL; // Should not happen
    }

    atomic_fetch_add_explicit(&pool->allocated, 1, memory_order_relaxed);

    // Zero the block before returning (caller expects zeroed memory)
    memset(block, 0, kClassSizes[class_idx]);

    return block;
}

void rt_pool_free(void *ptr, size_t size)
{
    if (!ptr)
        return;

    rt_pool_class_t class_idx = size_to_class(size);

    // Large allocations were from malloc
    if (class_idx >= RT_POOL_COUNT)
    {
        free(ptr);
        return;
    }

    rt_pool_state_t *pool = &g_pools[class_idx];

    // Clear the block before returning to pool (security/debugging)
    memset(ptr, 0, kClassSizes[class_idx]);

    // Push block back to freelist
    push_to_freelist(pool, (rt_pool_block_t *)ptr);

    atomic_fetch_sub_explicit(&pool->allocated, 1, memory_order_relaxed);
}

void rt_pool_stats(rt_pool_class_t class_idx, size_t *out_allocated, size_t *out_free)
{
    if (class_idx >= RT_POOL_COUNT)
    {
        if (out_allocated)
            *out_allocated = 0;
        if (out_free)
            *out_free = 0;
        return;
    }

    rt_pool_state_t *pool = &g_pools[class_idx];

    if (out_allocated)
        *out_allocated = atomic_load_explicit(&pool->allocated, memory_order_relaxed);
    if (out_free)
        *out_free = atomic_load_explicit(&pool->free_count, memory_order_relaxed);
}

void rt_pool_shutdown(void)
{
    for (int i = 0; i < RT_POOL_COUNT; i++)
    {
        rt_pool_state_t *pool = &g_pools[i];

        // Free all slabs
        rt_pool_slab_t *slab = pool->slabs;
        while (slab)
        {
            rt_pool_slab_t *next = slab->next;
            free(slab);
            slab = next;
        }

        // Reset state
        pool->slabs = NULL;
        atomic_store_explicit(&pool->freelist, NULL, memory_order_relaxed);
        atomic_store_explicit(&pool->allocated, 0, memory_order_relaxed);
        atomic_store_explicit(&pool->free_count, 0, memory_order_relaxed);
    }
}
