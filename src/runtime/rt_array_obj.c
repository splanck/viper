//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements dynamic arrays of object references (void*). Each element is an
// object handle managed by the object runtime (retain/release/free). The array
// itself is allocated through the shared heap and reference-counted.
//
//===----------------------------------------------------------------------===//

#include "rt_array_obj.h"

#include "rt_heap.h"
#include "rt_object.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static rt_heap_hdr_t *rt_arr_obj_hdr(void **payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

static void rt_arr_obj_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    // No dedicated elem_kind for object; use NONE to indicate generic pointer
    assert(hdr->elem_kind == RT_ELEM_NONE);
}

void **rt_arr_obj_new(size_t len)
{
    void **arr = (void **)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_NONE, sizeof(void *), len, len);
    if (arr && len)
        memset(arr, 0, len * sizeof(void *));
    return arr;
}

size_t rt_arr_obj_len(void **arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    rt_arr_obj_assert_header(hdr);
    return hdr->len;
}

void *rt_arr_obj_get(void **arr, size_t idx)
{
    assert(arr != NULL);
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    rt_arr_obj_assert_header(hdr);
    assert(idx < hdr->len);
    void *p = arr[idx];
    rt_obj_retain_maybe(p);
    return p;
}

void rt_arr_obj_put(void **arr, size_t idx, void *obj)
{
    assert(arr != NULL);
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    rt_arr_obj_assert_header(hdr);
    assert(idx < hdr->len);

    // Retain new first to handle self-assignment safely
    rt_obj_retain_maybe(obj);

    void *old = arr[idx];
    arr[idx] = obj;
    if (old)
    {
        if (rt_obj_release_check0(old))
            rt_obj_free(old);
    }
}

void **rt_arr_obj_resize(void **arr, size_t len)
{
    if (!arr)
        return rt_arr_obj_new(len);

    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    rt_arr_obj_assert_header(hdr);

    size_t new_cap = len;
    // compute total bytes with overflow checks similar to rt_arr_i32
    if (new_cap > 0 && new_cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(void *))
        return NULL;

    size_t payload_bytes = new_cap * sizeof(void *);
    size_t total_bytes = sizeof(rt_heap_hdr_t) + payload_bytes;

    rt_heap_hdr_t *resized = (rt_heap_hdr_t *)realloc(hdr, total_bytes);
    if (!resized && total_bytes != 0)
        return NULL;

    void **payload = (void **)rt_heap_data(resized);
    size_t old_len = resized->len;
    if (len > old_len)
    {
        size_t grow = len - old_len;
        memset(payload + old_len, 0, grow * sizeof(void *));
    }
    resized->cap = new_cap;
    resized->len = len;

    return payload;
}

void rt_arr_obj_release(void **arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    rt_arr_obj_assert_header(hdr);

    size_t n = hdr->len;
    for (size_t i = 0; i < n; ++i)
    {
        void *p = arr[i];
        if (p)
        {
            if (rt_obj_release_check0(p))
                rt_obj_free(p);
            arr[i] = NULL;
        }
    }
    rt_heap_release(arr);
}
