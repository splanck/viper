//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements dynamic arrays of object references (void*). Each element is an
// object handle managed by the object runtime (retain/release/free). The array
// itself is allocated through the shared heap and reference-counted.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements dynamic arrays of object references for the runtime.
/// @details Each element is a runtime-managed object pointer. The array owns
///          references to its elements and is responsible for retaining on
///          insertion and releasing on overwrite or teardown.

#include "rt_array_obj.h"

#include "rt_heap.h"
#include "rt_object.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/// @brief Return the heap header associated with an object array payload.
/// @details The payload pointer is the first element of the array; the header
///          is stored immediately before it in the heap allocation.
/// @param payload Array payload pointer (element 0) or NULL.
/// @return Heap header pointer, or NULL if @p payload is NULL.
static rt_heap_hdr_t *rt_arr_obj_hdr(void **payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

/// @brief Assert that a heap header describes an object array.
/// @details Verifies allocation kind and element kind to catch misuse early.
/// @param hdr Heap header to validate (must be non-NULL).
static void rt_arr_obj_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    // No dedicated elem_kind for object; use NONE to indicate generic pointer
    assert(hdr->elem_kind == RT_ELEM_NONE);
}

/// @brief Allocate a new object array with logical length @p len.
/// @details The payload is zero-initialized so all elements start as NULL.
///          The returned pointer is the payload (element 0), not the header.
/// @param len Number of elements to allocate.
/// @return Payload pointer for the new array, or NULL on allocation failure.
void **rt_arr_obj_new(size_t len)
{
    void **arr = (void **)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_NONE, sizeof(void *), len, len);
    if (arr && len)
        memset(arr, 0, len * sizeof(void *));
    return arr;
}

/// @brief Return the logical length of the object array.
/// @details A NULL array is treated as length zero for convenience.
/// @param arr Object array payload pointer (may be NULL).
/// @return Number of elements in the array, or 0 if @p arr is NULL.
size_t rt_arr_obj_len(void **arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_obj_hdr(arr);
    rt_arr_obj_assert_header(hdr);
    return hdr->len;
}

/// @brief Retrieve an element and retain it for the caller.
/// @details The returned object reference is retained so the caller owns a
///          reference independent of subsequent array mutations. The function
///          asserts that @p arr is non-NULL and @p idx is in range.
/// @param arr Object array payload pointer (must be non-NULL).
/// @param idx Element index to read.
/// @return Retained object pointer (may be NULL if the slot is empty).
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

/// @brief Store an object reference at the specified index.
/// @details Retains the new object before overwriting the slot so self-assignment
///          is safe. Releases the previous object reference and frees it if its
///          reference count drops to zero. The function asserts that @p arr is
///          non-NULL and @p idx is in range.
/// @param arr Object array payload pointer (must be non-NULL).
/// @param idx Element index to update.
/// @param obj Object reference to store (may be NULL).
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

/// @brief Resize an object array to the requested length.
/// @details When growing, new elements are zero-initialized. When shrinking,
///          the logical length is reduced without releasing truncated elements,
///          so callers should release or clear elements explicitly if required.
///          The array may move in memory due to reallocation.
/// @param arr Existing array payload pointer (may be NULL).
/// @param len New logical length.
/// @return Payload pointer for the resized array, or NULL on failure.
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

/// @brief Release all elements and the array itself.
/// @details Each non-NULL element is released and freed when its reference count
///          drops to zero. The array payload is then released via the heap API.
/// @param arr Object array payload pointer (may be NULL).
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
