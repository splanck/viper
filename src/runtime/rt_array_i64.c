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

/// @file
/// @brief Implements dynamic array helpers for 64-bit integer values.
/// @details Provides allocation, bounds-checked access, and resize logic for
///          arrays of int64_t stored in the runtime heap.

#include "rt_array_i64.h"
#include "rt_array.h" // for rt_arr_oob_panic
#include "rt_internal.h"
#include "rt_platform.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Return the heap header associated with an int64 array payload.
/// @details The payload pointer refers to element 0; the header precedes it in
///          memory and encodes length, capacity, and element kind.
/// @param payload Array payload pointer or NULL.
/// @return Heap header pointer, or NULL if @p payload is NULL.
rt_heap_hdr_t *rt_arr_i64_hdr(const int64_t *payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

// Generate standard array helper functions using macros from rt_internal.h
RT_ARR_DEFINE_ASSERT_HEADER_FN(rt_arr_i64_assert_header, RT_ELEM_I64)
RT_ARR_DEFINE_PAYLOAD_BYTES_FN(rt_arr_i64_payload_bytes, int64_t)

// Relaxed assertion for 64-bit numeric arrays (I64 or F64)
// Used for operations that don't depend on element type (len, retain, release)
/// @brief Assert that a header is a 64-bit numeric array (I64 or F64).
/// @details Some operations (retain/release/len) are valid for both types.
/// @param hdr Heap header to validate (must be non-NULL).
static void rt_arr_64bit_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_I64 || hdr->elem_kind == RT_ELEM_F64);
}

/// @brief Validate array bounds and panic on out-of-range access.
/// @details Traps via @ref rt_arr_oob_panic when @p arr is NULL or @p idx is
///          beyond the current logical length.
/// @param arr Array payload pointer.
/// @param idx Element index to validate.
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

/// @brief Allocate a new array of int64 values with length @p len.
/// @details Allocates a runtime heap array with matching length and capacity.
/// @param len Number of elements to allocate.
/// @return Payload pointer or NULL on allocation failure.
int64_t *rt_arr_i64_new(size_t len)
{
    return (int64_t *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_I64, sizeof(int64_t), len, len);
}

/// @brief Increment the reference count on the array payload.
/// @details Accepts both I64 and F64 arrays for shared retain logic. No-op
///          when @p arr is NULL.
/// @param arr Array payload pointer.
void rt_arr_i64_retain(int64_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_64bit_assert_header(hdr); // Accept both I64 and F64 for retain
    rt_heap_retain(arr);
}

/// @brief Decrement the reference count and free on zero.
/// @details Accepts both I64 and F64 arrays for shared release logic. No-op
///          when @p arr is NULL.
/// @param arr Array payload pointer.
void rt_arr_i64_release(int64_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_64bit_assert_header(hdr); // Accept both I64 and F64 for release
    rt_heap_release(arr);
}

/// @brief Return the logical length of the array.
/// @details Accepts both I64 and F64 arrays for shared length queries. Returns
///          0 for NULL arrays.
/// @param arr Array payload pointer (may be NULL).
/// @return Number of elements in the array.
size_t rt_arr_i64_len(int64_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_64bit_assert_header(hdr); // Accept both I64 and F64 for len
    return hdr->len;
}

/// @brief Return the current capacity of the array.
/// @details Returns 0 for NULL arrays.
/// @param arr Array payload pointer (may be NULL).
/// @return Capacity in elements.
size_t rt_arr_i64_cap(int64_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i64_hdr(arr);
    rt_arr_i64_assert_header(hdr);
    return hdr->cap;
}

/// @brief Read an element with bounds checking.
/// @details Traps on NULL arrays or out-of-range indices.
/// @param arr Array payload pointer.
/// @param idx Element index to read.
/// @return The element value at @p idx.
int64_t rt_arr_i64_get(int64_t *arr, size_t idx)
{
    rt_arr_i64_validate_bounds(arr, idx);
    return arr[idx];
}

/// @brief Write an element with bounds checking.
/// @details Traps on NULL arrays or out-of-range indices.
/// @param arr Array payload pointer.
/// @param idx Element index to write.
/// @param value Value to store.
void rt_arr_i64_set(int64_t *arr, size_t idx, int64_t value)
{
    rt_arr_i64_validate_bounds(arr, idx);
    arr[idx] = value;
}

/// @brief Copy a sequence of elements between payload buffers.
/// @details Traps when either pointer is NULL and @p count is non-zero.
/// @param dst Destination payload pointer.
/// @param src Source payload pointer.
/// @param count Number of elements to copy.
void rt_arr_i64_copy_payload(int64_t *dst, const int64_t *src, size_t count)
{
    if (count == 0)
        return;

    if (!dst || !src)
        rt_arr_oob_panic(0, count);

    memcpy(dst, src, count * sizeof(int64_t));
}

// Generate grow_in_place and resize functions using macros from rt_internal.h
RT_ARR_DEFINE_GROW_IN_PLACE_FN(rt_arr_i64_grow_in_place, int64_t, rt_arr_i64_payload_bytes)
RT_ARR_DEFINE_RESIZE_FN(rt_arr_i64_resize, int64_t, rt_arr_i64_hdr, rt_arr_i64_assert_header,
                        rt_arr_i64_new, rt_arr_i64_copy_payload, rt_arr_i64_release,
                        rt_arr_i64_grow_in_place)
