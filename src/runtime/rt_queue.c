//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_queue.c
// Purpose: Implement Viper.Collections.Queue - a FIFO (first-in-first-out) collection.
//
// Structure:
// - Implemented as a circular buffer for O(1) add/take operations
// - Internal representation uses head/tail indices with wrap-around
// - Automatic growth when capacity is exceeded
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

#define QUEUE_DEFAULT_CAP 16
#define QUEUE_GROWTH_FACTOR 2

/// Internal queue structure (circular buffer).
typedef struct rt_queue_impl
{
    int64_t len;  ///< Number of elements currently in the queue
    int64_t cap;  ///< Current capacity (allocated slots)
    int64_t head; ///< Index of first element (front of queue)
    int64_t tail; ///< Index where next element will be inserted (back of queue)
    void **items; ///< Circular buffer of element pointers
} rt_queue_impl;

static void rt_queue_finalize(void *obj)
{
    if (!obj)
        return;
    rt_queue_impl *q = (rt_queue_impl *)obj;
    free(q->items);
    q->items = NULL;
    q->len = 0;
    q->cap = 0;
    q->head = 0;
    q->tail = 0;
}

/// @brief Grow the queue capacity and linearize the circular buffer.
/// @param q Queue to grow.
static void queue_grow(rt_queue_impl *q)
{
    int64_t new_cap = q->cap * QUEUE_GROWTH_FACTOR;
    void **new_items = malloc((size_t)new_cap * sizeof(void *));

    if (!new_items)
    {
        rt_trap("Queue: memory allocation failed");
    }

    // Linearize the circular buffer into the new array
    if (q->len > 0)
    {
        if (q->head < q->tail)
        {
            // Contiguous region: head...tail
            memcpy(new_items, &q->items[q->head], (size_t)q->len * sizeof(void *));
        }
        else
        {
            // Wrapped around: head...end, then start...tail
            int64_t first_part = q->cap - q->head;
            memcpy(new_items, &q->items[q->head], (size_t)first_part * sizeof(void *));
            memcpy(&new_items[first_part], q->items, (size_t)q->tail * sizeof(void *));
        }
    }

    free(q->items);
    q->items = new_items;
    q->head = 0;
    q->tail = q->len;
    q->cap = new_cap;
}

/// @brief Create a new empty queue with default capacity.
/// @return New queue object.
void *rt_queue_new(void)
{
    rt_queue_impl *q = (rt_queue_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_queue_impl));
    if (!q)
    {
        rt_trap("Queue: memory allocation failed");
    }

    q->len = 0;
    q->cap = QUEUE_DEFAULT_CAP;
    q->head = 0;
    q->tail = 0;
    q->items = malloc((size_t)QUEUE_DEFAULT_CAP * sizeof(void *));
    rt_obj_set_finalizer(q, rt_queue_finalize);

    if (!q->items)
    {
        if (rt_obj_release_check0(q))
            rt_obj_free(q);
        rt_trap("Queue: memory allocation failed");
    }

    return q;
}

/// @brief Get the number of elements in the queue.
/// @param obj Queue object.
/// @return Number of elements.
int64_t rt_queue_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_queue_impl *)obj)->len;
}

/// @brief Check if the queue is empty.
/// @param obj Queue object.
/// @return 1 if empty, 0 otherwise.
int8_t rt_queue_is_empty(void *obj)
{
    if (!obj)
        return 1;
    return ((rt_queue_impl *)obj)->len == 0 ? 1 : 0;
}

/// @brief Add an element to the back of the queue.
/// @param obj Queue object.
/// @param val Element to add.
void rt_queue_add(void *obj, void *val)
{
    if (!obj)
        rt_trap("Queue.Add: null queue");

    rt_queue_impl *q = (rt_queue_impl *)obj;

    if (q->len >= q->cap)
    {
        queue_grow(q);
    }

    q->items[q->tail] = val;
    q->tail = (q->tail + 1) % q->cap;
    q->len++;
}

/// @brief Remove and return the front element from the queue.
/// @param obj Queue object.
/// @return The removed element.
void *rt_queue_take(void *obj)
{
    if (!obj)
        rt_trap("Queue.Take: null queue");

    rt_queue_impl *q = (rt_queue_impl *)obj;

    if (q->len == 0)
    {
        rt_trap("Queue.Take: queue is empty");
    }

    void *val = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->len--;

    return val;
}

/// @brief Return the front element without removing it.
/// @param obj Queue object.
/// @return The front element.
void *rt_queue_peek(void *obj)
{
    if (!obj)
        rt_trap("Queue.Peek: null queue");

    rt_queue_impl *q = (rt_queue_impl *)obj;

    if (q->len == 0)
    {
        rt_trap("Queue.Peek: queue is empty");
    }

    return q->items[q->head];
}

/// @brief Remove all elements from the queue.
/// @param obj Queue object.
void rt_queue_clear(void *obj)
{
    if (!obj)
        return;

    rt_queue_impl *q = (rt_queue_impl *)obj;
    q->len = 0;
    q->head = 0;
    q->tail = 0;
}
