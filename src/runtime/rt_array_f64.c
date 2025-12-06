//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the dynamic array helpers for 64-bit floating-point arrays.
// This mirrors rt_array_i64.c but uses double elements.
//
//===----------------------------------------------------------------------===//

#include "rt_array_f64.h"
#include "rt_array.h"  // for rt_arr_oob_panic

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

rt_heap_hdr_t *rt_arr_f64_hdr(const double *payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

static void rt_arr_f64_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_F64);
}

static void rt_arr_f64_validate_bounds(double *arr, size_t idx)
{
    if (!arr)
        rt_arr_oob_panic(idx, 0);

    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);

    size_t len = hdr->len;
    if (idx >= len)
        rt_arr_oob_panic(idx, len);
}

static size_t rt_arr_f64_payload_bytes(size_t cap)
{
    if (cap == 0)
        return 0;
    if (cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(double))
        return 0;
    return cap * sizeof(double);
}

double *rt_arr_f64_new(size_t len)
{
    return (double *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_F64, sizeof(double), len, len);
}

void rt_arr_f64_retain(double *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);
    rt_heap_retain(arr);
}

void rt_arr_f64_release(double *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);
    rt_heap_release(arr);
}

size_t rt_arr_f64_len(double *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);
    return hdr->len;
}

size_t rt_arr_f64_cap(double *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);
    return hdr->cap;
}

double rt_arr_f64_get(double *arr, size_t idx)
{
    rt_arr_f64_validate_bounds(arr, idx);
    return arr[idx];
}

void rt_arr_f64_set(double *arr, size_t idx, double value)
{
    rt_arr_f64_validate_bounds(arr, idx);
    arr[idx] = value;
}

void rt_arr_f64_copy_payload(double *dst, const double *src, size_t count)
{
    if (count == 0)
        return;

    if (!dst || !src)
        rt_arr_oob_panic(0, count);

    memcpy(dst, src, count * sizeof(double));
}

static int rt_arr_f64_grow_in_place(rt_heap_hdr_t **hdr_inout,
                                    double **payload_inout,
                                    size_t new_len)
{
    rt_heap_hdr_t *hdr = *hdr_inout;
    size_t old_len = hdr ? hdr->len : 0;
    size_t new_cap = new_len;
    size_t payload_bytes = rt_arr_f64_payload_bytes(new_cap);
    if (new_cap > 0 && payload_bytes == 0)
        return -1;

    size_t total_bytes = sizeof(rt_heap_hdr_t) + payload_bytes;
    rt_heap_hdr_t *resized = (rt_heap_hdr_t *)realloc(hdr, total_bytes);
    if (!resized)
        return -1;

    double *payload = (double *)rt_heap_data(resized);
    if (new_len > old_len)
    {
        size_t grow = new_len - old_len;
        memset(payload + old_len, 0, grow * sizeof(double));
    }
    resized->cap = new_cap;
    resized->len = new_len;

    *hdr_inout = resized;
    *payload_inout = payload;
    return 0;
}

int rt_arr_f64_resize(double **a_inout, size_t new_len)
{
    if (!a_inout)
        return -1;

    double *arr = *a_inout;
    if (!arr)
    {
        double *fresh = rt_arr_f64_new(new_len);
        if (!fresh)
            return -1;
        *a_inout = fresh;
        return 0;
    }

    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);

    size_t old_len = hdr->len;
    size_t cap = hdr->cap;
    if (new_len <= cap)
    {
        if (new_len > old_len)
            memset(arr + old_len, 0, (new_len - old_len) * sizeof(double));
        rt_heap_set_len(arr, new_len);
        return 0;
    }

    if (hdr->refcnt > 1)
    {
        double *fresh = rt_arr_f64_new(new_len);
        if (!fresh)
            return -1;
        size_t copy_len = old_len < new_len ? old_len : new_len;
        rt_arr_f64_copy_payload(fresh, arr, copy_len);
        rt_arr_f64_release(arr);
        *a_inout = fresh;
        return 0;
    }

    rt_heap_hdr_t *hdr_mut = hdr;
    double *payload = arr;
    if (rt_arr_f64_grow_in_place(&hdr_mut, &payload, new_len) != 0)
        return -1;
    *a_inout = payload;
    return 0;
}
