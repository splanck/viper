//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_list_i64.c
// Purpose: Dynamic-append list of 64-bit integers without boxing (P2-3.7).
// Key invariants: len <= cap; push is amortized O(1); refcount == 1 on new.
// Ownership/Lifetime: Caller owns the initial reference from rt_list_i64_new.
//                     Push may reallocate; callers must use the updated pointer.
//
//===----------------------------------------------------------------------===//

#include "rt_list_i64.h"

#include "rt_array.h"    // rt_arr_oob_panic
#include "rt_heap.h"     // rt_heap_alloc, rt_heap_hdr, rt_heap_set_len

#include <assert.h>
#include <string.h>

/// Minimum allocation capacity when init_cap is zero.
#define RT_LIST_I64_MIN_CAP 8

/// @brief Allocate an empty int64 list with pre-reserved capacity.
/// @details Allocates via the runtime heap with len=0 and cap=max(init_cap,8).
///          The first push will not allocate as long as the initial capacity
///          has not been exhausted.
/// @param init_cap Desired initial capacity in elements; 0 defaults to 8.
/// @return List payload pointer (element 0 address), or NULL on failure.
int64_t *rt_list_i64_new(size_t init_cap)
{
    size_t cap = init_cap ? init_cap : RT_LIST_I64_MIN_CAP;
    return (int64_t *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_I64, sizeof(int64_t), 0, cap);
}

/// @brief Append @p val to the list, growing the buffer if needed.
/// @details Fast path: len < cap — write directly and bump len in the header.
///          Slow path: cap exhausted — allocate a new buffer with 2× capacity,
///          copy existing elements, write @p val, release the old buffer, and
///          update @p *list_inout with the new address.
/// @param list_inout Non-null pointer to the list payload; updated on realloc.
/// @param val Value to append.
/// @return 0 on success; -1 when the slow-path allocation fails.
int rt_list_i64_push(int64_t **list_inout, int64_t val)
{
    assert(list_inout && *list_inout);

    int64_t *arr = *list_inout;
    rt_heap_hdr_t *hdr = rt_heap_hdr(arr);
    size_t len = hdr->len;
    size_t cap = hdr->cap;

    if (len < cap)
    {
        // Fast path: capacity available — no allocation needed.
        arr[len] = val;
        rt_heap_set_len(arr, len + 1);
        return 0;
    }

    // Slow path: double the capacity (or bootstrap to the minimum).
    size_t new_cap = cap ? cap * 2 : RT_LIST_I64_MIN_CAP;
    int64_t *new_arr =
        (int64_t *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_I64, sizeof(int64_t), 0, new_cap);
    if (!new_arr)
        return -1;

    if (len)
        memcpy(new_arr, arr, len * sizeof(int64_t));

    new_arr[len] = val;
    rt_heap_set_len(new_arr, len + 1);

    // Release old buffer and redirect the caller's pointer.
    rt_arr_i64_release(arr);
    *list_inout = new_arr;
    return 0;
}

/// @brief Remove and return the last element.
/// @details Traps via rt_arr_oob_panic when the list is empty. Decrements len
///          in the header; does not release capacity.
/// @param list_inout Non-null pointer to the list payload.
/// @return Value of the removed element.
int64_t rt_list_i64_pop(int64_t **list_inout)
{
    assert(list_inout && *list_inout);

    int64_t *arr = *list_inout;
    rt_heap_hdr_t *hdr = rt_heap_hdr(arr);
    size_t len = hdr->len;

    if (len == 0)
        rt_arr_oob_panic(0, 0);

    int64_t val = arr[len - 1];
    rt_heap_set_len(arr, len - 1);
    return val;
}

/// @brief Return the last element without removing it.
/// @details Traps via rt_arr_oob_panic when the list is empty.
/// @param list Non-null list payload pointer.
/// @return Value of the last element.
int64_t rt_list_i64_peek(int64_t *list)
{
    assert(list);

    rt_heap_hdr_t *hdr = rt_heap_hdr(list);
    size_t len = hdr->len;

    if (len == 0)
        rt_arr_oob_panic(0, 0);

    return list[len - 1];
}
