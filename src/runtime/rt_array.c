// File: src/runtime/rt_array.c
// Purpose: Implements dynamic int32 array helpers for the BASIC runtime.
// Key invariants: Array length never exceeds capacity; allocations are zeroed on growth.
// Ownership/Lifetime: Arrays participate in shared reference-counted heap management.
// Links: docs/codemap.md

#include "rt_array.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Helper returning the heap header associated with @p payload.
/// @param payload Array payload pointer (may be NULL).
/// @return Heap header describing the allocation, or NULL for NULL payloads.
rt_heap_hdr_t *rt_arr_i32_hdr(const int32_t *payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

void rt_arr_oob_panic(size_t idx, size_t len)
{
    fprintf(stderr, "rt_arr_i32: index %zu out of bounds (len=%zu)\n", idx, len);
    abort();
}

static void rt_arr_i32_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_I32);
}

static void rt_arr_i32_validate_bounds(int32_t *arr, size_t idx)
{
    if (!arr)
        rt_arr_oob_panic(idx, 0);

    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);

    size_t len = hdr->len;
    if (idx >= len)
        rt_arr_oob_panic(idx, len);
}

static size_t rt_arr_i32_payload_bytes(size_t cap)
{
    if (cap == 0)
        return 0;
    if (cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(int32_t))
        return 0;
    return cap * sizeof(int32_t);
}

int32_t *rt_arr_i32_new(size_t len)
{
    return (int32_t *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_I32, sizeof(int32_t), len, len);
}

void rt_arr_i32_retain(int32_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    rt_heap_retain(arr);
}

void rt_arr_i32_release(int32_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    rt_heap_release(arr);
}

size_t rt_arr_i32_len(int32_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    return hdr->len;
}

size_t rt_arr_i32_cap(int32_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    return hdr->cap;
}

int32_t rt_arr_i32_get(int32_t *arr, size_t idx)
{
    rt_arr_i32_validate_bounds(arr, idx);
    return arr[idx];
}

void rt_arr_i32_set(int32_t *arr, size_t idx, int32_t value)
{
    rt_arr_i32_validate_bounds(arr, idx);
    arr[idx] = value;
}

/// @brief Copy @p count elements, aborting when presented with invalid buffers.
/// @param dst Destination array payload; must be non-null when @p count > 0.
/// @param src Source array payload; must be non-null when @p count > 0.
/// @param count Number of elements to copy.
void rt_arr_i32_copy_payload(int32_t *dst, const int32_t *src, size_t count)
{
    if (count == 0)
        return;

    if (!dst || !src)
        rt_arr_oob_panic(0, count);

    memcpy(dst, src, count * sizeof(int32_t));
}

static int rt_arr_i32_grow_in_place(rt_heap_hdr_t **hdr_inout,
                                    int32_t **payload_inout,
                                    size_t new_len)
{
    rt_heap_hdr_t *hdr = *hdr_inout;
    size_t new_cap = new_len;
    size_t payload_bytes = rt_arr_i32_payload_bytes(new_cap);
    if (new_cap > 0 && payload_bytes == 0)
        return -1;

    size_t total_bytes = sizeof(rt_heap_hdr_t) + payload_bytes;
    rt_heap_hdr_t *resized = (rt_heap_hdr_t *)realloc(hdr, total_bytes);
    if (!resized)
        return -1;

    int32_t *payload = (int32_t *)rt_heap_data(resized);
    size_t old_len = resized->len;
    if (new_len > old_len)
    {
        size_t grow = new_len - old_len;
        memset(payload + old_len, 0, grow * sizeof(int32_t));
    }
    resized->cap = new_cap;
    resized->len = new_len;

    *hdr_inout = resized;
    *payload_inout = payload;
    return 0;
}

int rt_arr_i32_resize(int32_t **a_inout, size_t new_len)
{
    if (!a_inout)
        return -1;

    int32_t *arr = *a_inout;
    if (!arr)
    {
        int32_t *fresh = rt_arr_i32_new(new_len);
        if (!fresh)
            return -1;
        *a_inout = fresh;
        return 0;
    }

    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);

    size_t old_len = hdr->len;
    size_t cap = hdr->cap;
    if (new_len <= cap)
    {
        if (new_len > old_len)
            memset(arr + old_len, 0, (new_len - old_len) * sizeof(int32_t));
        rt_heap_set_len(arr, new_len);
        return 0;
    }

    if (hdr->refcnt > 1)
    {
        int32_t *fresh = rt_arr_i32_new(new_len);
        if (!fresh)
            return -1;
        size_t copy_len = old_len < new_len ? old_len : new_len;
        // rt_arr_i32_new and the existing array guarantee non-null payloads when copy_len > 0.
        rt_arr_i32_copy_payload(fresh, arr, copy_len);
        rt_arr_i32_release(arr);
        *a_inout = fresh;
        return 0;
    }

    rt_heap_hdr_t *hdr_mut = hdr;
    int32_t *payload = arr;
    if (rt_arr_i32_grow_in_place(&hdr_mut, &payload, new_len) != 0)
        return -1;
    *a_inout = payload;
    return 0;
}
