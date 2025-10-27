//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime's dynamic @c int32_t array helpers.  The
// routines in this translation unit wrap the shared heap allocator so BASIC
// arrays participate in reference counting, bounds checking, and zero-initialised
// growth that mirrors the VM implementation.  Consolidating the helpers here
// keeps all array semantics—length queries, resizing, and payload access—in sync
// across native and interpreted execution.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Dynamic array helpers backing BASIC integer collections.
/// @details Defines constructors, retain/release operations, and mutation
///          utilities that manage @c int32_t payloads stored in the shared
///          runtime heap.  The helpers enforce BASIC-specific invariants such as
///          zero-filling on growth and aborting on out-of-bounds access.

#include "rt_array.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Retrieve the heap header associated with an array payload.
/// @details Returns @c NULL when @p payload is @c NULL so callers can perform
///          optional chaining without branching.  Otherwise forwards to
///          @ref rt_heap_hdr.
/// @param payload Array element pointer obtained from an @ref rt_heap_alloc call.
/// @return Heap header for @p payload or @c NULL for null payloads.
rt_heap_hdr_t *rt_arr_i32_hdr(const int32_t *payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

/// @brief Abort the process with a descriptive out-of-bounds message.
/// @details Emits the offending @p idx and the array length before calling
///          @c abort so debugging failing programs remains straightforward.
/// @param idx Index that violated bounds.
/// @param len Length of the array when the violation occurred.
void rt_arr_oob_panic(size_t idx, size_t len)
{
    fprintf(stderr, "rt_arr_i32: index %zu out of bounds (len=%zu)\n", idx, len);
    abort();
}

/// @brief Validate that a heap header describes an @c int32_t array.
/// @details Used internally to assert that runtime handles were produced by the
///          expected allocator before mutating metadata.
/// @param hdr Heap header returned by @ref rt_arr_i32_hdr; must be non-null.
static void rt_arr_i32_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_I32);
}

/// @brief Ensure @p idx is within the current bounds of @p arr.
/// @details Retrieves the heap header, validates its metadata, and raises a
///          panic when @p idx falls outside the array length.
/// @param arr Array payload pointer.
/// @param idx Index to validate.
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

/// @brief Compute the number of payload bytes required for a given capacity.
/// @details Guards against overflow when multiplying by @c sizeof(int32_t) and
///          returns zero when the capacity would exceed @c size_t limits.
/// @param cap Requested number of elements.
/// @return Byte count required to store @p cap elements, or zero on overflow.
static size_t rt_arr_i32_payload_bytes(size_t cap)
{
    if (cap == 0)
        return 0;
    if (cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(int32_t))
        return 0;
    return cap * sizeof(int32_t);
}

/// @brief Allocate a new array with @p len elements, zero-initialised.
/// @details Delegates to @ref rt_heap_alloc so the resulting payload participates
///          in reference counting and length tracking.
/// @param len Number of elements requested.
/// @return Pointer to the array payload or @c NULL on allocation failure.
int32_t *rt_arr_i32_new(size_t len)
{
    return (int32_t *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_I32, sizeof(int32_t), len, len);
}

/// @brief Retain an array payload when sharing it across runtime values.
/// @details Increments the heap reference count when @p arr is non-null,
///          preserving BASIC's copy-on-write semantics.
/// @param arr Array payload to retain; @c NULL is ignored.
void rt_arr_i32_retain(int32_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    rt_heap_retain(arr);
}

/// @brief Release an array payload previously retained.
/// @details Decrements the shared reference count and frees the payload when it
///          reaches zero.  Null payloads are ignored.
/// @param arr Array payload to release; @c NULL is ignored.
void rt_arr_i32_release(int32_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    rt_heap_release(arr);
}

/// @brief Query the number of logical elements in an array.
/// @details Returns zero for null payloads so callers can operate on optional
///          handles without branching.
/// @param arr Array payload pointer; may be @c NULL.
/// @return Number of elements currently stored.
size_t rt_arr_i32_len(int32_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    return hdr->len;
}

/// @brief Query the capacity (in elements) of an array payload.
/// @details Mirrors @ref rt_arr_i32_len but inspects the stored capacity.
/// @param arr Array payload pointer; may be @c NULL.
/// @return Number of elements that fit without reallocation.
size_t rt_arr_i32_cap(int32_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    return hdr->cap;
}

/// @brief Load an element from an array with bounds checking.
/// @details Validates @p idx via @ref rt_arr_i32_validate_bounds before reading.
/// @param arr Array payload pointer.
/// @param idx Element index to load.
/// @return The value stored at @p idx.
int32_t rt_arr_i32_get(int32_t *arr, size_t idx)
{
    rt_arr_i32_validate_bounds(arr, idx);
    return arr[idx];
}

/// @brief Store an element in an array with bounds checking.
/// @details Ensures @p idx is valid before writing @p value into the payload.
/// @param arr Array payload pointer.
/// @param idx Element index to modify.
/// @param value Value to store.
void rt_arr_i32_set(int32_t *arr, size_t idx, int32_t value)
{
    rt_arr_i32_validate_bounds(arr, idx);
    arr[idx] = value;
}

/// @brief Copy @p count elements between payload buffers.
/// @details Used by resizing logic to clone existing contents.  The helper
///          aborts when presented with null buffers and a non-zero count so bugs
///          surface loudly during development.
/// @param dst Destination payload; must be valid when @p count > 0.
/// @param src Source payload; must be valid when @p count > 0.
/// @param count Number of elements to copy.
void rt_arr_i32_copy_payload(int32_t *dst, const int32_t *src, size_t count)
{
    if (count == 0)
        return;

    if (!dst || !src)
        rt_arr_oob_panic(0, count);

    memcpy(dst, src, count * sizeof(int32_t));
}

/// @brief Attempt to grow an array in place.
/// @details Reallocates the heap block referenced by @p hdr_inout to satisfy the
///          new length, zero-initialising any newly exposed elements.  The
///          function updates both the header pointer and payload pointer on
///          success.
/// @param hdr_inout Heap header pointer to update on success.
/// @param payload_inout Payload pointer to update on success.
/// @param new_len Desired number of logical elements.
/// @return @c 0 on success, @c -1 when allocation fails or overflow is detected.
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

/// @brief Resize an array, allocating or copying as needed.
/// @details Handles null arrays by allocating a fresh payload, performs
///          copy-on-write when the payload is shared, and otherwise attempts to
///          grow in place.  Newly exposed elements are zeroed to satisfy BASIC's
///          expectations.
/// @param a_inout Pointer to the array payload pointer to update.
/// @param new_len Target length in elements.
/// @return @c 0 on success or @c -1 when allocation fails.
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
