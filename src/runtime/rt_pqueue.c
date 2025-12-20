//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_heap.c
// Purpose: Implement Viper.Collections.Heap - a priority queue using binary heap.
//
// Structure:
// - Implemented as a binary heap stored in a dynamic array
// - Each element is a (priority, value) pair
// - Supports both min-heap (smallest priority first) and max-heap modes
// - Automatic growth when capacity is exceeded
//
//===----------------------------------------------------------------------===//

#include "rt_pqueue.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stdlib.h>
#include <string.h>

#define HEAP_DEFAULT_CAP 16
#define HEAP_GROWTH_FACTOR 2

/// @brief A single entry in the heap containing priority and value.
typedef struct heap_entry
{
    int64_t priority; ///< Priority value (lower = higher priority for min-heap)
    void *value;      ///< The stored object
} heap_entry;

/// @brief Internal heap implementation structure.
///
/// The Heap is implemented as a binary heap stored in a dynamic array.
/// For a min-heap, the smallest priority value is at the root (index 0).
/// For a max-heap, the largest priority value is at the root.
///
/// **Binary heap property:**
/// - Parent at index i has children at indices 2*i+1 and 2*i+2
/// - Each parent has priority >= (max-heap) or <= (min-heap) its children
///
/// **Memory layout example (min-heap with 5 elements):**
/// ```
///                  [0]
///                 /    \
///              [1]      [2]
///             /   \
///          [3]     [4]
///
/// Array: [(1,A), (3,B), (2,C), (5,D), (4,E)]
///         ^root
/// ```
typedef struct rt_pqueue_impl
{
    int64_t len;      ///< Number of elements currently in the heap
    int64_t cap;      ///< Current capacity (allocated slots)
    int8_t is_max;    ///< 1 for max-heap, 0 for min-heap
    heap_entry *items; ///< Array of (priority, value) entries
} rt_pqueue_impl;

/// @brief Finalizer callback invoked when a Heap is garbage collected.
static void rt_pqueue_finalize(void *obj)
{
    if (!obj)
        return;
    rt_pqueue_impl *h = (rt_pqueue_impl *)obj;
    free(h->items);
    h->items = NULL;
    h->len = 0;
    h->cap = 0;
}

/// @brief Grows the heap capacity.
static void heap_grow(rt_pqueue_impl *h)
{
    int64_t new_cap = h->cap * HEAP_GROWTH_FACTOR;
    heap_entry *new_items = malloc((size_t)new_cap * sizeof(heap_entry));

    if (!new_items)
    {
        rt_trap("Heap: memory allocation failed");
    }

    if (h->len > 0)
    {
        memcpy(new_items, h->items, (size_t)h->len * sizeof(heap_entry));
    }

    free(h->items);
    h->items = new_items;
    h->cap = new_cap;
}

/// @brief Compare two priorities based on heap type.
/// Returns true if a should be higher in the heap than b.
static inline int heap_compare(rt_pqueue_impl *h, int64_t a, int64_t b)
{
    if (h->is_max)
        return a > b; // Max-heap: larger values go up
    else
        return a < b; // Min-heap: smaller values go up
}

/// @brief Swap two entries in the heap.
static inline void heap_swap(heap_entry *items, int64_t i, int64_t j)
{
    heap_entry tmp = items[i];
    items[i] = items[j];
    items[j] = tmp;
}

/// @brief Restore heap property by moving an element up.
static void heap_swim(rt_pqueue_impl *h, int64_t k)
{
    while (k > 0)
    {
        int64_t parent = (k - 1) / 2;
        if (!heap_compare(h, h->items[k].priority, h->items[parent].priority))
            break;
        heap_swap(h->items, k, parent);
        k = parent;
    }
}

/// @brief Restore heap property by moving an element down.
static void heap_sink(rt_pqueue_impl *h, int64_t k)
{
    while (2 * k + 1 < h->len)
    {
        int64_t child = 2 * k + 1; // Left child
        // Pick the child with higher priority
        if (child + 1 < h->len &&
            heap_compare(h, h->items[child + 1].priority, h->items[child].priority))
        {
            child++; // Right child has higher priority
        }
        // If parent already has higher priority, stop
        if (!heap_compare(h, h->items[child].priority, h->items[k].priority))
            break;
        heap_swap(h->items, k, child);
        k = child;
    }
}

void *rt_pqueue_new(void)
{
    return rt_pqueue_new_max(0); // Default to min-heap
}

void *rt_pqueue_new_max(int8_t is_max)
{
    rt_pqueue_impl *h = (rt_pqueue_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_pqueue_impl));
    if (!h)
    {
        rt_trap("Heap: memory allocation failed");
    }

    h->len = 0;
    h->cap = HEAP_DEFAULT_CAP;
    h->is_max = is_max ? 1 : 0;
    h->items = malloc((size_t)HEAP_DEFAULT_CAP * sizeof(heap_entry));
    rt_obj_set_finalizer(h, rt_pqueue_finalize);

    if (!h->items)
    {
        if (rt_obj_release_check0(h))
            rt_obj_free(h);
        rt_trap("Heap: memory allocation failed");
    }

    return h;
}

int64_t rt_pqueue_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_pqueue_impl *)obj)->len;
}

int8_t rt_pqueue_is_empty(void *obj)
{
    if (!obj)
        return 1;
    return ((rt_pqueue_impl *)obj)->len == 0 ? 1 : 0;
}

int8_t rt_pqueue_is_max(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_pqueue_impl *)obj)->is_max;
}

void rt_pqueue_push(void *obj, int64_t priority, void *val)
{
    if (!obj)
        rt_trap("Heap.Push: null heap");

    rt_pqueue_impl *h = (rt_pqueue_impl *)obj;

    if (h->len >= h->cap)
    {
        heap_grow(h);
    }

    // Add at the end
    h->items[h->len].priority = priority;
    h->items[h->len].value = val;
    h->len++;

    // Restore heap property
    heap_swim(h, h->len - 1);
}

void *rt_pqueue_pop(void *obj)
{
    if (!obj)
        rt_trap("Heap.Pop: null heap");

    rt_pqueue_impl *h = (rt_pqueue_impl *)obj;

    if (h->len == 0)
    {
        rt_trap("Heap.Pop: heap is empty");
    }

    void *val = h->items[0].value;

    // Move last element to root and shrink
    h->len--;
    if (h->len > 0)
    {
        h->items[0] = h->items[h->len];
        heap_sink(h, 0);
    }

    return val;
}

void *rt_pqueue_peek(void *obj)
{
    if (!obj)
        rt_trap("Heap.Peek: null heap");

    rt_pqueue_impl *h = (rt_pqueue_impl *)obj;

    if (h->len == 0)
    {
        rt_trap("Heap.Peek: heap is empty");
    }

    return h->items[0].value;
}

void *rt_pqueue_try_pop(void *obj)
{
    if (!obj)
        return NULL;

    rt_pqueue_impl *h = (rt_pqueue_impl *)obj;

    if (h->len == 0)
        return NULL;

    void *val = h->items[0].value;

    h->len--;
    if (h->len > 0)
    {
        h->items[0] = h->items[h->len];
        heap_sink(h, 0);
    }

    return val;
}

void *rt_pqueue_try_peek(void *obj)
{
    if (!obj)
        return NULL;

    rt_pqueue_impl *h = (rt_pqueue_impl *)obj;

    if (h->len == 0)
        return NULL;

    return h->items[0].value;
}

void rt_pqueue_clear(void *obj)
{
    if (!obj)
        return;

    rt_pqueue_impl *h = (rt_pqueue_impl *)obj;
    h->len = 0;
}

void *rt_pqueue_to_seq(void *obj)
{
    if (!obj)
        rt_trap("Heap.ToSeq: null heap");

    rt_pqueue_impl *h = (rt_pqueue_impl *)obj;

    // Create a new Seq
    void *seq = rt_seq_new();

    // Pop all elements in priority order and add to Seq
    // We need to work on a copy to avoid destroying the original
    rt_pqueue_impl *copy = (rt_pqueue_impl *)rt_pqueue_new_max(h->is_max);
    
    // Copy all entries
    for (int64_t i = 0; i < h->len; i++)
    {
        rt_pqueue_push(copy, h->items[i].priority, h->items[i].value);
    }

    // Pop from copy in priority order
    while (copy->len > 0)
    {
        void *val = rt_pqueue_pop(copy);
        rt_seq_push(seq, val);
    }

    // Release the copy
    if (rt_obj_release_check0(copy))
        rt_obj_free(copy);

    return seq;
}
