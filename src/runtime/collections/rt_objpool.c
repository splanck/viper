//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_objpool.c
// Purpose: Fixed-capacity object pool for Viper games. Eliminates per-frame
//   allocation churn for frequently created and destroyed game objects such as
//   bullets, enemies, particles, and projectiles. Slots are acquired (checked
//   out) and released (checked in) in O(1) time using an embedded free-list.
//   Active slots are traversable in O(1) per step using an intrusive singly-
//   linked active list maintained by acquire and release.
//
// Key invariants:
//   - The pool owns a single calloc'd slot array of fixed capacity. Capacity is
//     clamped to RT_OBJPOOL_MAX (4096) at creation and never changes.
//   - Slot indices are stable across the pool's lifetime: an acquired slot keeps
//     the same integer index from acquire until release. Games may store extra
//     data per-slot via rt_objpool_set_data / rt_objpool_get_data (one int64).
//   - The free list is a singly-linked chain through pool_slot.next_free.
//     free_head is the index of the next available slot (-1 when pool is full).
//   - The active list is a singly-linked chain through pool_slot.next_active.
//     active_head is the index of the first acquired slot (-1 when empty).
//     rt_objpool_first_active / rt_objpool_next_active run in O(1).
//   - Release is O(active_count) in the worst case because the active list is
//     singly-linked and removing a non-head element requires scanning for the
//     predecessor. For typical game pool sizes (<= 512) this is negligible.
//   - Releasing an already-free slot (double-release) is a safe no-op.
//
// Ownership/Lifetime:
//   - The pool struct is GC-managed (rt_obj_new_i64). The slot array is
//     malloc'd separately; the GC finalizer (objpool_finalizer) frees it when
//     the pool is collected. rt_objpool_destroy() is a documented no-op for
//     API symmetry — do not rely on it to free memory.
//
// Links: src/runtime/collections/rt_objpool.h (public API),
//        docs/viperlib/game.md (ObjectPool section)
//
//===----------------------------------------------------------------------===//

#include "rt_objpool.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// Internal slot structure.
struct pool_slot
{
    int64_t data;        ///< User data.
    int64_t next_free;   ///< Next free slot index (-1 if end).
    int64_t next_active; ///< Next active slot index (-1 if tail of active list).
    int8_t active;       ///< 1 if acquired, 0 if free.
};

/// Internal pool structure.
struct rt_objpool_impl
{
    struct pool_slot *slots; ///< Slot array.
    int64_t capacity;        ///< Total capacity.
    int64_t active_count;    ///< Number of active slots.
    int64_t free_head;       ///< Head of free list.
    int64_t active_head;     ///< Head of active list (-1 if none). O(1) iteration.
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
    pool->active_head = -1;

    // Initialize free list
    for (int64_t i = 0; i < capacity; i++)
    {
        pool->slots[i].data = 0;
        pool->slots[i].active = 0;
        pool->slots[i].next_free = (i + 1 < capacity) ? i + 1 : -1;
        pool->slots[i].next_active = -1;
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
    // Prepend to active list (O(1))
    pool->slots[slot].next_active = pool->active_head;
    pool->active_head = slot;
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
    // Remove from active list (O(active_count))
    if (pool->active_head == slot)
    {
        pool->active_head = pool->slots[slot].next_active;
    }
    else
    {
        int64_t prev = pool->active_head;
        while (prev >= 0 && pool->slots[prev].next_active != slot)
            prev = pool->slots[prev].next_active;
        if (prev >= 0)
            pool->slots[prev].next_active = pool->slots[slot].next_active;
    }
    pool->slots[slot].next_active = -1;
    // Return to free list
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
    pool->active_head = -1;

    for (int64_t i = 0; i < pool->capacity; i++)
    {
        pool->slots[i].data = 0;
        pool->slots[i].active = 0;
        pool->slots[i].next_free = (i + 1 < pool->capacity) ? i + 1 : -1;
        pool->slots[i].next_active = -1;
    }
}

int64_t rt_objpool_first_active(rt_objpool pool)
{
    // O(1): return head of the maintained active list
    return pool ? pool->active_head : -1;
}

int64_t rt_objpool_next_active(rt_objpool pool, int64_t after)
{
    // O(1): follow the intrusive next_active pointer
    if (!pool || after < 0 || after >= pool->capacity)
        return -1;
    return pool->slots[after].next_active;
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
