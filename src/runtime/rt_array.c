//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the dynamic array helpers that back BASIC's `DIM` and collection
// operations.  The routines here coordinate with the shared runtime heap to
// provide reference-counted storage, bounds checking, and copy-on-write
// semantics that match the behaviour of the VM interpreter.  Keeping the logic
// centralised avoids divergence between the native runtime and the VM helpers
// when resizing or sharing arrays across procedures.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Reference-counted runtime array utilities.
/// @details Supplies creation, retention, mutation, and resize helpers for the
///          integer arrays exposed through the BASIC runtime ABI.  All
///          operations validate metadata emitted by the shared heap allocator to
///          guard against memory corruption and incorrect sharing semantics.

#include "rt_array.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Retrieve the heap header for a runtime array payload.
/// @details Delegates to @ref rt_heap_hdr while tolerating null payloads so
///          callers can interrogate optional handles without branching.
/// @param payload Array payload pointer (may be `NULL`).
/// @return Header describing the allocation, or `NULL` when @p payload is null.
rt_heap_hdr_t *rt_arr_i32_hdr(const int32_t *payload)
{
    return payload ? rt_heap_hdr((void *)payload) : NULL;
}

/// @brief Abort execution due to an out-of-bounds access.
/// @details Emits a descriptive error message to standard error before
///          terminating the process.  The helper is intentionally marked `noreturn`
///          through the implicit @ref abort call so callers can rely on it for
///          bounds enforcement in release builds as well.
/// @param idx Index that triggered the violation.
/// @param len Array length used for diagnostics.
void rt_arr_oob_panic(size_t idx, size_t len)
{
    fprintf(stderr, "rt_arr_i32: index %zu out of bounds (len=%zu)\n", idx, len);
    abort();
}

/// @brief Confirm that a heap header matches the expected array metadata.
/// @details Validates the heap kind and element kind so that higher level code
///          never manipulates the wrong allocation through the array helpers.
///          Firing assertions early helps pinpoint incorrect pointer sharing.
/// @param hdr Heap header retrieved from an array payload.
static void rt_arr_i32_assert_header(rt_heap_hdr_t *hdr)
{
    assert(hdr);
    assert(hdr->kind == RT_HEAP_ARRAY);
    assert(hdr->elem_kind == RT_ELEM_I32);
}

/// @brief Verify that an index falls inside the logical length of an array.
/// @details Checks the array pointer, confirms the backing header is valid, and
///          compares @p idx against the recorded length.  Violations delegate to
///          @ref rt_arr_oob_panic which aborts the program.
/// @param arr Runtime array payload.
/// @param idx Zero-based index being accessed.
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

/// @brief Compute the payload size in bytes for a requested capacity.
/// @details Guards against overflow when translating an element count into the
///          number of bytes required for the allocation.  Returning zero signals
///          that the request was invalid or empty.
/// @param cap Requested capacity in elements.
/// @return Number of bytes needed for the payload, or zero on overflow.
static size_t rt_arr_i32_payload_bytes(size_t cap)
{
    if (cap == 0)
        return 0;
    if (cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(int32_t))
        return 0;
    return cap * sizeof(int32_t);
}

/// @brief Allocate a new array with @p len elements.
/// @details Requests storage from the shared heap allocator and returns the
///          payload pointer.  The allocation is zero-initialised and tracks both
///          logical length and capacity via the heap header.
/// @param len Requested element count; the capacity matches this value.
/// @return Pointer to the array payload, or `NULL` when allocation fails.
int32_t *rt_arr_i32_new(size_t len)
{
    return (int32_t *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_I32, sizeof(int32_t), len, len);
}

/// @brief Increase the reference count for an array payload.
/// @details Guards against null pointers and validates the heap metadata before
///          delegating to @ref rt_heap_retain.
/// @param arr Array payload pointer.
void rt_arr_i32_retain(int32_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    rt_heap_retain(arr);
}

/// @brief Decrease the reference count for an array payload.
/// @details Validates the associated heap header prior to delegating to
///          @ref rt_heap_release so copy-on-write invariants remain intact.
/// @param arr Array payload pointer.
void rt_arr_i32_release(int32_t *arr)
{
    if (!arr)
        return;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    rt_heap_release(arr);
}

/// @brief Retrieve the logical element count for the array.
/// @details Returns zero for null arrays so callers can handle optional
///          references without extra branching.
/// @param arr Array payload pointer.
/// @return Number of elements currently in use.
size_t rt_arr_i32_len(int32_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    return hdr->len;
}

/// @brief Retrieve the reserved capacity for the array.
/// @param arr Array payload pointer.
/// @return Number of elements that fit without reallocating.
size_t rt_arr_i32_cap(int32_t *arr)
{
    if (!arr)
        return 0;
    rt_heap_hdr_t *hdr = rt_arr_i32_hdr(arr);
    rt_arr_i32_assert_header(hdr);
    return hdr->cap;
}

/// @brief Read an element after checking bounds.
/// @param arr Array payload pointer.
/// @param idx Zero-based index to access.
/// @return Value stored at the requested index.
int32_t rt_arr_i32_get(int32_t *arr, size_t idx)
{
    rt_arr_i32_validate_bounds(arr, idx);
    return arr[idx];
}

/// @brief Write an element after checking bounds.
/// @param arr Array payload pointer.
/// @param idx Zero-based index to write.
/// @param value Value to store in the array.
void rt_arr_i32_set(int32_t *arr, size_t idx, int32_t value)
{
    rt_arr_i32_validate_bounds(arr, idx);
    arr[idx] = value;
}

/// @brief Copy @p count elements between array payloads.
/// @details Validates that both payloads are non-null when copying a non-empty
///          range and then performs a `memcpy`.  Bounds are assumed to have been
///          checked by the caller.
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

/// @brief Attempt to grow an array in place, reallocating the underlying block.
/// @details Recomputes the payload capacity, performs a `realloc`, and zeros the
///          newly exposed region when the array has grown.  On success both the
///          header pointer and payload pointer are updated to point at the new
///          storage.
/// @param hdr_inout Pointer to the header pointer that will be updated in place.
/// @param payload_inout Pointer to the payload pointer updated in place.
/// @param new_len New logical length requested by the caller.
/// @return Zero on success; -1 on allocation failure or overflow.
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

/// @brief Resize an array handle to the requested length.
/// @details Handles the `NULL` array case by allocating a fresh buffer, performs
///          copy-on-write when the array is shared, and otherwise grows the
///          allocation in place.  New elements are zero-initialised to match the
///          runtime specification.
/// @param a_inout Pointer to the array payload handle updated in place.
/// @param new_len Target logical length of the array.
/// @return Zero on success; -1 on allocation failure or invalid arguments.
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
