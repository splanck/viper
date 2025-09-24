// File: src/runtime/rt_array.c
// Purpose: Implements dynamic int32 array helpers for the BASIC runtime.
// Key invariants: Array length never exceeds capacity; allocations are zeroed on growth.
// Ownership/Lifetime: Caller manages array handles and releases them via free().
// Links: docs/codemap.md

#include "rt_array.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rt_arr_i32_impl
{
    size_t len;
    size_t cap;
    int32_t data[];
};

typedef struct rt_arr_i32_impl rt_arr_i32_impl;

static int compute_allocation_bytes(size_t cap, size_t *total_bytes)
{
    if (!total_bytes)
        return 0;
    if (cap == 0)
    {
        *total_bytes = sizeof(rt_arr_i32_impl);
        return 1;
    }
    if (cap > (SIZE_MAX - sizeof(rt_arr_i32_impl)) / sizeof(int32_t))
        return 0;
    size_t data_bytes = cap * sizeof(int32_t);
    *total_bytes = sizeof(rt_arr_i32_impl) + data_bytes;
    return 1;
}

void *rt_arr_i32_new(size_t len)
{
    size_t total_bytes = 0;
    if (!compute_allocation_bytes(len, &total_bytes))
        return NULL;
    rt_arr_i32_impl *arr = (rt_arr_i32_impl *)calloc(1, total_bytes);
    if (!arr)
        return NULL;
    arr->len = len;
    arr->cap = len;
    return arr;
}

size_t rt_arr_i32_len(const void *arr)
{
    const rt_arr_i32_impl *impl = (const rt_arr_i32_impl *)arr;
    return impl ? impl->len : 0;
}

int32_t rt_arr_i32_get(const void *arr, size_t idx)
{
    const rt_arr_i32_impl *impl = (const rt_arr_i32_impl *)arr;
    return impl->data[idx];
}

void rt_arr_i32_set(void *arr, size_t idx, int32_t value)
{
    rt_arr_i32_impl *impl = (rt_arr_i32_impl *)arr;
    impl->data[idx] = value;
}

void *rt_arr_i32_resize(void *arr, size_t new_len)
{
    rt_arr_i32_impl *impl = (rt_arr_i32_impl *)arr;
    if (!impl)
        return rt_arr_i32_new(new_len);

    size_t old_len = impl->len;
    if (new_len <= impl->cap)
    {
        if (new_len > old_len)
        {
            size_t grow = new_len - old_len;
            memset(impl->data + old_len, 0, grow * sizeof(int32_t));
        }
        impl->len = new_len;
        return impl;
    }

    size_t total_bytes = 0;
    if (!compute_allocation_bytes(new_len, &total_bytes))
        return NULL;

    rt_arr_i32_impl *resized = (rt_arr_i32_impl *)realloc(impl, total_bytes);
    if (!resized)
        return NULL;

    if (new_len > old_len)
    {
        size_t grow = new_len - old_len;
        memset(resized->data + old_len, 0, grow * sizeof(int32_t));
    }

    resized->len = new_len;
    resized->cap = new_len;
    return resized;
}

void rt_arr_oob_panic(size_t idx, size_t len)
{
    fprintf(stderr, "rt_arr_i32: index %zu out of bounds (len=%zu)\n", idx, len);
    abort();
}

