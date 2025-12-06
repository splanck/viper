//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the dynamic array helpers for 64-bit integer arrays (LONG).
// This mirrors rt_array.c but uses int64_t elements.
//
//===----------------------------------------------------------------------===//

#include "rt_array_i64.h"
#include "rt_array.h"  // for rt_arr_oob_panic

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

rt_heap_hdr_t *rt_arr_i64_hdr(const int64_t *payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

static void rt_arr_i64_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_I64);
}

// Relaxed assertion for 64-bit numeric arrays (I64 or F64)
// Used for operations that don't depend on element type (len, retain, release)
static void rt_arr_64bit_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_I64 || hdr->elem_kind == RT_ELEM_F64);
}

static void rt_arr_i64_validate_bounds(int64_t *arr, size_t idx)
{
    if (!arr)
        rt_arr_oob_panic(idx, 0);

    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_i64_assert_header(hdr);

    size_t len = hdr->len;
    if (idx >= len)
        rt_arr_oob_panic(idx, len);
}

static size_t rt_arr_i64_payload_bytes(size_t cap)
{
    if (cap == 0)
        return 0;
    if (cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(int64_t))
        return 0;
    return cap * sizeof(int64_t);
}

int64_t *rt_arr_i64_new(size_t len)
{
    return (int64_t *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_I64, sizeof(int64_t), len, len);
}

void rt_arr_i64_retain(int64_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_64bit_assert_header(hdr);  // Accept both I64 and F64 for retain
    rt_heap_retain(arr);
}

void rt_arr_i64_release(int64_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_64bit_assert_header(hdr);  // Accept both I64 and F64 for release
    rt_heap_release(arr);
}

size_t rt_arr_i64_len(int64_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_64bit_assert_header(hdr);  // Accept both I64 and F64 for len
    return hdr->len;
}

size_t rt_arr_i64_cap(int64_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_i64_assert_header(hdr);
    return hdr->cap;
}

int64_t rt_arr_i64_get(int64_t *arr, size_t idx)
{
    rt_arr_i64_validate_bounds(arr, idx);
    return arr[idx];
}

void rt_arr_i64_set(int64_t *arr, size_t idx, int64_t value)
{
    rt_arr_i64_validate_bounds(arr, idx);
    arr[idx] = value;
}

void rt_arr_i64_copy_payload(int64_t *dst, const int64_t *src, size_t count)
{
    if (count == 0)
        return;

    if (!dst || !src)
        rt_arr_oob_panic(0, count);

    memcpy(dst, src, count * sizeof(int64_t));
}

static int rt_arr_i64_grow_in_place(rt_heap_hdr_t **hdr_inout,
                                    int64_t **payload_inout,
                                    size_t new_len)
{
    rt_heap_hdr_t *hdr = *hdr_inout;
    size_t old_len = hdr ? hdr->len : 0;
    size_t new_cap = new_len;
    size_t payload_bytes = rt_arr_i64_payload_bytes(new_cap);
    if (new_cap > 0 && payload_bytes == 0)
        return -1;

    size_t total_bytes = sizeof(rt_heap_hdr_t) + payload_bytes;
    rt_heap_hdr_t *resized = (rt_heap_hdr_t *)realloc(hdr, total_bytes);
    if (!resized)
        return -1;

    int64_t *payload = (int64_t *)rt_heap_data(resized);
    if (new_len > old_len)
    {
        size_t grow = new_len - old_len;
        memset(payload + old_len, 0, grow * sizeof(int64_t));
    }
    resized->cap = new_cap;
    resized->len = new_len;

    *hdr_inout = resized;
    *payload_inout = payload;
    return 0;
}

int rt_arr_i64_resize(int64_t **a_inout, size_t new_len)
{
    if (!a_inout)
        return -1;

    int64_t *arr = *a_inout;
    if (!arr)
    {
        int64_t *fresh = rt_arr_i64_new(new_len);
        if (!fresh)
            return -1;
        *a_inout = fresh;
        return 0;
    }

    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_i64_assert_header(hdr);

    size_t old_len = hdr->len;
    size_t cap = hdr->cap;
    if (new_len <= cap)
    {
        if (new_len > old_len)
            memset(arr + old_len, 0, (new_len - old_len) * sizeof(int64_t));
        rt_heap_set_len(arr, new_len);
        return 0;
    }

    if (hdr->refcnt > 1)
    {
        int64_t *fresh = rt_arr_i64_new(new_len);
        if (!fresh)
            return -1;
        size_t copy_len = old_len < new_len ? old_len : new_len;
        rt_arr_i64_copy_payload(fresh, arr, copy_len);
        rt_arr_i64_release(arr);
        *a_inout = fresh;
        return 0;
    }

    rt_heap_hdr_t *hdr_mut = hdr;
    int64_t *payload = arr;
    if (rt_arr_i64_grow_in_place(&hdr_mut, &payload, new_len) != 0)
        return -1;
    *a_inout = payload;
    return 0;
}
