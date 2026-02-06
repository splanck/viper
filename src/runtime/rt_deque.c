//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_deque.c
// Purpose: Double-ended queue implementation using circular buffer.
//
//===----------------------------------------------------------------------===//

#include "rt_deque.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structure
//=============================================================================

#define DEFAULT_CAPACITY 16

typedef struct
{
    void **data;   // Circular buffer
    int64_t cap;   // Capacity
    int64_t len;   // Number of elements
    int64_t front; // Index of front element
} Deque;

//=============================================================================
// Helper Functions
//=============================================================================

static void trap_with_message(const char *msg)
{
    fprintf(stderr, "Deque trap: %s\n", msg);
    abort();
}

static void ensure_capacity(Deque *d, int64_t required)
{
    if (required <= d->cap)
        return;

    int64_t new_cap = d->cap * 2;
    if (new_cap < required)
        new_cap = required;

    void **new_data = (void **)malloc((size_t)new_cap * sizeof(void *));
    if (!new_data)
        trap_with_message("Failed to allocate memory for deque");

    // Copy elements to new array, starting at index 0
    for (int64_t i = 0; i < d->len; i++)
    {
        int64_t idx = (d->front + i) % d->cap;
        new_data[i] = d->data[idx];
    }

    free(d->data);
    d->data = new_data;
    d->cap = new_cap;
    d->front = 0;
}

//=============================================================================
// Deque Creation
//=============================================================================

void *rt_deque_new(void)
{
    return rt_deque_with_capacity(DEFAULT_CAPACITY);
}

void *rt_deque_with_capacity(int64_t cap)
{
    if (cap < 1)
        cap = 1;

    Deque *d = (Deque *)malloc(sizeof(Deque));
    if (!d)
        return NULL;

    d->data = (void **)malloc((size_t)cap * sizeof(void *));
    if (!d->data)
    {
        free(d);
        return NULL;
    }

    d->cap = cap;
    d->len = 0;
    d->front = 0;
    return d;
}

//=============================================================================
// Size Operations
//=============================================================================

int64_t rt_deque_len(void *obj)
{
    if (!obj)
        return 0;
    Deque *d = (Deque *)obj;
    return d->len;
}

int64_t rt_deque_cap(void *obj)
{
    if (!obj)
        return 0;
    Deque *d = (Deque *)obj;
    return d->cap;
}

int8_t rt_deque_is_empty(void *obj)
{
    if (!obj)
        return 1;
    Deque *d = (Deque *)obj;
    return d->len == 0 ? 1 : 0;
}

//=============================================================================
// Front Operations
//=============================================================================

void rt_deque_push_front(void *obj, void *val)
{
    if (!obj)
        return;
    Deque *d = (Deque *)obj;

    ensure_capacity(d, d->len + 1);

    // Move front pointer backward (with wrap-around)
    d->front = (d->front - 1 + d->cap) % d->cap;
    d->data[d->front] = val;
    d->len++;
}

void *rt_deque_pop_front(void *obj)
{
    if (!obj)
        trap_with_message("PopFront called on NULL deque");
    Deque *d = (Deque *)obj;
    if (d->len == 0)
        trap_with_message("PopFront called on empty deque");

    void *val = d->data[d->front];
    d->front = (d->front + 1) % d->cap;
    d->len--;
    return val;
}

void *rt_deque_peek_front(void *obj)
{
    if (!obj)
        trap_with_message("PeekFront called on NULL deque");
    Deque *d = (Deque *)obj;
    if (d->len == 0)
        trap_with_message("PeekFront called on empty deque");

    return d->data[d->front];
}

//=============================================================================
// Back Operations
//=============================================================================

void rt_deque_push_back(void *obj, void *val)
{
    if (!obj)
        return;
    Deque *d = (Deque *)obj;

    ensure_capacity(d, d->len + 1);

    int64_t back = (d->front + d->len) % d->cap;
    d->data[back] = val;
    d->len++;
}

void *rt_deque_pop_back(void *obj)
{
    if (!obj)
        trap_with_message("PopBack called on NULL deque");
    Deque *d = (Deque *)obj;
    if (d->len == 0)
        trap_with_message("PopBack called on empty deque");

    int64_t back = (d->front + d->len - 1) % d->cap;
    void *val = d->data[back];
    d->len--;
    return val;
}

void *rt_deque_peek_back(void *obj)
{
    if (!obj)
        trap_with_message("PeekBack called on NULL deque");
    Deque *d = (Deque *)obj;
    if (d->len == 0)
        trap_with_message("PeekBack called on empty deque");

    int64_t back = (d->front + d->len - 1) % d->cap;
    return d->data[back];
}

//=============================================================================
// Random Access
//=============================================================================

void *rt_deque_get(void *obj, int64_t idx)
{
    if (!obj)
        trap_with_message("Get called on NULL deque");
    Deque *d = (Deque *)obj;
    if (idx < 0 || idx >= d->len)
        trap_with_message("Index out of bounds");

    int64_t actual = (d->front + idx) % d->cap;
    return d->data[actual];
}

void rt_deque_set(void *obj, int64_t idx, void *val)
{
    if (!obj)
        trap_with_message("Set called on NULL deque");
    Deque *d = (Deque *)obj;
    if (idx < 0 || idx >= d->len)
        trap_with_message("Index out of bounds");

    int64_t actual = (d->front + idx) % d->cap;
    d->data[actual] = val;
}

//=============================================================================
// Utility
//=============================================================================

void rt_deque_clear(void *obj)
{
    if (!obj)
        return;
    Deque *d = (Deque *)obj;
    d->len = 0;
    d->front = 0;
}

int8_t rt_deque_has(void *obj, void *val)
{
    if (!obj)
        return 0;
    Deque *d = (Deque *)obj;

    for (int64_t i = 0; i < d->len; i++)
    {
        int64_t idx = (d->front + i) % d->cap;
        if (d->data[idx] == val)
            return 1;
    }
    return 0;
}

void rt_deque_reverse(void *obj)
{
    if (!obj)
        return;
    Deque *d = (Deque *)obj;
    if (d->len < 2)
        return;

    for (int64_t i = 0; i < d->len / 2; i++)
    {
        int64_t front_idx = (d->front + i) % d->cap;
        int64_t back_idx = (d->front + d->len - 1 - i) % d->cap;

        void *tmp = d->data[front_idx];
        d->data[front_idx] = d->data[back_idx];
        d->data[back_idx] = tmp;
    }
}

void *rt_deque_clone(void *obj)
{
    if (!obj)
        return rt_deque_new();
    Deque *d = (Deque *)obj;

    void *new_d = rt_deque_with_capacity(d->cap);
    if (!new_d)
        return NULL;

    for (int64_t i = 0; i < d->len; i++)
    {
        int64_t idx = (d->front + i) % d->cap;
        rt_deque_push_back(new_d, d->data[idx]);
    }

    return new_d;
}
