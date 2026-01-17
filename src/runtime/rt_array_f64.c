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

/// @file
/// @brief Implements dynamic array helpers for 64-bit floating-point values.
/// @details Provides allocation, bounds-checked access, and resize logic for
///          arrays of doubles stored in the runtime heap.

#include "rt_array_f64.h"
#include "rt_array.h" // for rt_arr_oob_panic
#include "rt_platform.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Return the heap header associated with a double array payload.
/// @details The payload pointer refers to element 0; the header precedes it in
///          memory and encodes length, capacity, and element kind.
/// @param payload Array payload pointer or NULL.
/// @return Heap header pointer, or NULL if @p payload is NULL.
rt_heap_hdr_t *rt_arr_f64_hdr(const double *payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

/// @brief Assert that a heap header describes an F64 array.
/// @details Validates allocation kind and element kind to detect misuse.
/// @param hdr Heap header to validate (must be non-NULL).
static void rt_arr_f64_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_F64);
}

/// @brief Validate array bounds and panic on out-of-range access.
/// @details Traps via @ref rt_arr_oob_panic when @p arr is NULL or @p idx is
///          beyond the current logical length.
/// @param arr Array payload pointer.
/// @param idx Element index to validate.
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

/// @brief Compute payload byte size for a given capacity.
/// @details Returns 0 on overflow or when @p cap is zero.
/// @param cap Desired element capacity.
/// @return Payload size in bytes, or 0 on overflow.
static size_t rt_arr_f64_payload_bytes(size_t cap)
{
    if (cap == 0)
        return 0;
    if (cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(double))
        return 0;
    return cap * sizeof(double);
}

/// @brief Allocate a new array of doubles with length @p len.
/// @details Allocates a runtime heap array with matching length and capacity.
/// @param len Number of elements to allocate.
/// @return Payload pointer or NULL on allocation failure.
double *rt_arr_f64_new(size_t len)
{
    return (double *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_F64, sizeof(double), len, len);
}

/// @brief Increment the reference count on the array payload.
/// @details No-op when @p arr is NULL.
/// @param arr Array payload pointer.
void rt_arr_f64_retain(double *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);
    rt_heap_retain(arr);
}

/// @brief Decrement the reference count and free on zero.
/// @details No-op when @p arr is NULL.
/// @param arr Array payload pointer.
void rt_arr_f64_release(double *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);
    rt_heap_release(arr);
}

/// @brief Return the logical length of the array.
/// @details Returns 0 for NULL arrays for convenience.
/// @param arr Array payload pointer (may be NULL).
/// @return Number of elements in the array.
size_t rt_arr_f64_len(double *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);
    return hdr->len;
}

/// @brief Return the current capacity of the array.
/// @details Returns 0 for NULL arrays.
/// @param arr Array payload pointer (may be NULL).
/// @return Capacity in elements.
size_t rt_arr_f64_cap(double *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_f64_hdr(arr);
    rt_arr_f64_assert_header(hdr);
    return hdr->cap;
}

/// @brief Read an element with bounds checking.
/// @details Traps on NULL arrays or out-of-range indices.
/// @param arr Array payload pointer.
/// @param idx Element index to read.
/// @return The element value at @p idx.
double rt_arr_f64_get(double *arr, size_t idx)
{
    rt_arr_f64_validate_bounds(arr, idx);
    return arr[idx];
}

/// @brief Write an element with bounds checking.
/// @details Traps on NULL arrays or out-of-range indices.
/// @param arr Array payload pointer.
/// @param idx Element index to write.
/// @param value Value to store.
void rt_arr_f64_set(double *arr, size_t idx, double value)
{
    rt_arr_f64_validate_bounds(arr, idx);
    arr[idx] = value;
}

/// @brief Copy a sequence of elements between payload buffers.
/// @details Traps when either pointer is NULL and @p count is non-zero.
/// @param dst Destination payload pointer.
/// @param src Source payload pointer.
/// @param count Number of elements to copy.
void rt_arr_f64_copy_payload(double *dst, const double *src, size_t count)
{
    if (count == 0)
        return;

    if (!dst || !src)
        rt_arr_oob_panic(0, count);

    memcpy(dst, src, count * sizeof(double));
}

/// @brief Resize the backing allocation in place when possible.
/// @details Reallocates the combined header+payload block and zeroes any newly
///          added elements. Returns -1 on allocation or overflow failure.
/// @param hdr_inout In/out pointer to the array header.
/// @param payload_inout In/out pointer to the array payload.
/// @param new_len New logical length and capacity.
/// @return 0 on success, -1 on failure.
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

/// @brief Resize an array with copy-on-resize semantics.
/// @details If the array is shared (refcount > 1), a new allocation is created
///          and elements are copied into it before releasing the old payload.
///          When growing in place, new elements are zero-initialized.
/// @param a_inout Pointer to the payload pointer to update.
/// @param new_len Desired logical length.
/// @return 0 on success, -1 on allocation or parameter failure.
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

    if (__atomic_load_n(&hdr->refcnt, __ATOMIC_RELAXED) > 1)
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
