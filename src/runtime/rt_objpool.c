//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_objpool.c
/// @brief Implementation of object pool for efficient object reuse.
///
//===----------------------------------------------------------------------===//

#include "rt_objpool.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// Internal slot structure.
struct pool_slot
{
    int64_t data;      ///< User data.
    int64_t next_free; ///< Next free slot index (-1 if end).
    int8_t active;     ///< 1 if acquired, 0 if free.
};

/// Internal pool structure.
struct rt_objpool_impl
{
    struct pool_slot *slots; ///< Slot array.
    int64_t capacity;        ///< Total capacity.
    int64_t active_count;    ///< Number of active slots.
    int64_t free_head;       ///< Head of free list.
};

static void objpool_finalizer(void *obj)
{
    struct rt_objpool_impl *pool = (struct rt_objpool_impl *)obj;
    free(pool->slots);
    pool->slots = NULL;
}

rt_objpool rt_objpool_new(int64_t capacity)
{
    if (capacity < 1)
        capacity = 1;
    if (capacity > RT_OBJPOOL_MAX)
        capacity = RT_OBJPOOL_MAX;

    struct rt_objpool_impl *pool = rt_obj_new_i64(0, sizeof(struct rt_objpool_impl));
    if (!pool)
        return NULL;

    pool->slots = calloc((size_t)capacity, sizeof(struct pool_slot));
    if (!pool->slots)
    {
        return NULL;
    }

    pool->capacity = capacity;
    pool->active_count = 0;
    pool->free_head = 0;

    // Initialize free list
    for (int64_t i = 0; i < capacity; i++)
    {
        pool->slots[i].data = 0;
        pool->slots[i].active = 0;
        pool->slots[i].next_free = (i + 1 < capacity) ? i + 1 : -1;
    }

    rt_obj_set_finalizer(pool, objpool_finalizer);
    return pool;
}

void rt_objpool_destroy(rt_objpool pool)
{
    // Object is GC-managed; finalizer frees internal data.
    (void)pool;
}

int64_t rt_objpool_acquire(rt_objpool pool)
{
    if (!pool)
        return -1;
    if (pool->free_head < 0)
        return -1; // Pool is full

    int64_t slot = pool->free_head;
    pool->free_head = pool->slots[slot].next_free;
    pool->slots[slot].active = 1;
    pool->slots[slot].next_free = -1;
    pool->slots[slot].data = 0;
    pool->active_count++;

    return slot;
}

int8_t rt_objpool_release(rt_objpool pool, int64_t slot)
{
    if (!pool)
        return 0;
    if (slot < 0 || slot >= pool->capacity)
        return 0;
    if (!pool->slots[slot].active)
        return 0; // Already free

    pool->slots[slot].active = 0;
    pool->slots[slot].data = 0;
    pool->slots[slot].next_free = pool->free_head;
    pool->free_head = slot;
    pool->active_count--;

    return 1;
}

int8_t rt_objpool_is_active(rt_objpool pool, int64_t slot)
{
    if (!pool)
        return 0;
    if (slot < 0 || slot >= pool->capacity)
        return 0;
    return pool->slots[slot].active;
}

int64_t rt_objpool_active_count(rt_objpool pool)
{
    return pool ? pool->active_count : 0;
}

int64_t rt_objpool_free_count(rt_objpool pool)
{
    return pool ? pool->capacity - pool->active_count : 0;
}

int64_t rt_objpool_capacity(rt_objpool pool)
{
    return pool ? pool->capacity : 0;
}

int8_t rt_objpool_is_full(rt_objpool pool)
{
    return pool ? (pool->active_count >= pool->capacity ? 1 : 0) : 1;
}

int8_t rt_objpool_is_empty(rt_objpool pool)
{
    return pool ? (pool->active_count == 0 ? 1 : 0) : 1;
}

void rt_objpool_clear(rt_objpool pool)
{
    if (!pool)
        return;

    pool->active_count = 0;
    pool->free_head = 0;

    for (int64_t i = 0; i < pool->capacity; i++)
    {
        pool->slots[i].data = 0;
        pool->slots[i].active = 0;
        pool->slots[i].next_free = (i + 1 < pool->capacity) ? i + 1 : -1;
    }
}

int64_t rt_objpool_first_active(rt_objpool pool)
{
    if (!pool)
        return -1;
    for (int64_t i = 0; i < pool->capacity; i++)
    {
        if (pool->slots[i].active)
            return i;
    }
    return -1;
}

int64_t rt_objpool_next_active(rt_objpool pool, int64_t after)
{
    if (!pool)
        return -1;
    for (int64_t i = after + 1; i < pool->capacity; i++)
    {
        if (pool->slots[i].active)
            return i;
    }
    return -1;
}

int8_t rt_objpool_set_data(rt_objpool pool, int64_t slot, int64_t data)
{
    if (!pool)
        return 0;
    if (slot < 0 || slot >= pool->capacity)
        return 0;
    if (!pool->slots[slot].active)
        return 0;
    pool->slots[slot].data = data;
    return 1;
}

int64_t rt_objpool_get_data(rt_objpool pool, int64_t slot)
{
    if (!pool)
        return 0;
    if (slot < 0 || slot >= pool->capacity)
        return 0;
    return pool->slots[slot].data;
}
