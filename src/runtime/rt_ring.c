//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_ring.c
// Purpose: Implement a fixed-size circular buffer (ring buffer).
// Structure: [vptr | items | capacity | head | count]
// - vptr: points to class vtable (placeholder for OOP compatibility)
// - items: array of element pointers
// - capacity: fixed maximum size
// - head: index of oldest element
// - count: number of elements currently stored
//
// Behavior:
// - Push adds to tail; if full, overwrites oldest (head advances)
// - Pop removes from head (FIFO order)
// - Get(0) returns oldest, Get(len-1) returns newest
//
//===----------------------------------------------------------------------===//

#include "rt_ring.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// @brief Ring buffer implementation structure.
typedef struct rt_ring_impl
{
    void **vptr;    ///< Vtable pointer placeholder.
    void **items;   ///< Array of element pointers.
    size_t capacity; ///< Maximum number of elements.
    size_t head;    ///< Index of oldest element.
    size_t count;   ///< Number of elements currently stored.
} rt_ring_impl;

static void rt_ring_finalize(void *obj)
{
    if (!obj)
        return;
    rt_ring_impl *ring = (rt_ring_impl *)obj;
    // Note: We don't release contained items - container doesn't own them
    // (same behavior as Stack and Queue)
    free(ring->items);
    ring->items = NULL;
    ring->capacity = 0;
    ring->head = 0;
    ring->count = 0;
}

void *rt_ring_new(int64_t capacity)
{
    if (capacity <= 0)
        capacity = 1; // Minimum capacity of 1

    rt_ring_impl *ring = (rt_ring_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_ring_impl));
    if (!ring)
        return NULL;

    ring->vptr = NULL;
    ring->items = (void **)calloc((size_t)capacity, sizeof(void *));
    if (!ring->items)
    {
        ring->capacity = 0;
        ring->head = 0;
        ring->count = 0;
        rt_obj_set_finalizer(ring, rt_ring_finalize);
        return ring;
    }
    ring->capacity = (size_t)capacity;
    ring->head = 0;
    ring->count = 0;
    rt_obj_set_finalizer(ring, rt_ring_finalize);
    return ring;
}

int64_t rt_ring_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_ring_impl *)obj)->count;
}

int64_t rt_ring_cap(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_ring_impl *)obj)->capacity;
}

int8_t rt_ring_is_empty(void *obj)
{
    return rt_ring_len(obj) == 0;
}

int8_t rt_ring_is_full(void *obj)
{
    if (!obj)
        return 0;
    rt_ring_impl *ring = (rt_ring_impl *)obj;
    return ring->count == ring->capacity;
}

void rt_ring_push(void *obj, void *item)
{
    if (!obj)
        return;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (ring->capacity == 0 || !ring->items)
        return;

    // Calculate tail position (where new element goes)
    size_t tail = (ring->head + ring->count) % ring->capacity;

    if (ring->count == ring->capacity)
    {
        // Ring is full - overwrite oldest element at head position
        ring->items[ring->head] = item;
        // Advance head to next oldest
        ring->head = (ring->head + 1) % ring->capacity;
        // count stays the same (still full)
    }
    else
    {
        // Ring has space - add to tail
        ring->items[tail] = item;
        ring->count++;
    }
}

void *rt_ring_pop(void *obj)
{
    if (!obj)
        return NULL;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (ring->count == 0 || !ring->items)
        return NULL;

    // Get oldest element (at head)
    void *item = ring->items[ring->head];
    ring->items[ring->head] = NULL;

    // Advance head
    ring->head = (ring->head + 1) % ring->capacity;
    ring->count--;

    // Note: We don't release here - caller takes ownership
    return item;
}

void *rt_ring_peek(void *obj)
{
    if (!obj)
        return NULL;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (ring->count == 0 || !ring->items)
        return NULL;

    // Return oldest element without removing
    return ring->items[ring->head];
}

void *rt_ring_get(void *obj, int64_t index)
{
    if (!obj)
        return NULL;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (index < 0 || (size_t)index >= ring->count || !ring->items)
        return NULL;

    // Calculate actual index: logical 0 = head (oldest)
    size_t actual = (ring->head + (size_t)index) % ring->capacity;
    return ring->items[actual];
}

void rt_ring_clear(void *obj)
{
    if (!obj)
        return;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (!ring->items)
        return;

    // Clear element pointers (container doesn't own them)
    for (size_t i = 0; i < ring->count; i++)
    {
        size_t idx = (ring->head + i) % ring->capacity;
        ring->items[idx] = NULL;
    }

    ring->head = 0;
    ring->count = 0;
}
