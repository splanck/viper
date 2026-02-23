//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/arrays/rt_array_str.c
// Purpose: Implements dynamic string array helpers backing BASIC string array
//          (DIM s$()) operations. Each element is a reference-counted rt_string
//          handle; the array manages retain/release for every element as they
//          are inserted, overwritten, or the array is freed.
//
// Key invariants:
//   - Each rt_string element is independently reference-counted.
//   - Writing to a slot releases the previous string and retains the new one.
//   - Array teardown releases all string references exactly once.
//   - The array itself is reference-counted through the heap allocator.
//   - Out-of-bounds accesses trigger rt_arr_oob_panic and abort.
//   - Empty/NULL string slots are treated as empty strings, not null pointers.
//
// Ownership/Lifetime:
//   - The array holds strong references to all stored strings.
//   - Callers that retrieve elements receive borrowed references and must not
//     release them without first retaining.
//   - The array is freed through the heap refcount API, which triggers element
//     release in the finalizer.
//
// Links: src/runtime/arrays/rt_array_str.h (public API),
//        src/runtime/arrays/rt_array.h (int32 base module, oob_panic),
//        src/runtime/rt_string.h (rt_string retain/release)
//
//===----------------------------------------------------------------------===//

#include "rt_array_str.h"
#include "rt_internal.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// Generate standard array helper functions using macros from rt_internal.h
RT_ARR_DEFINE_HDR_FN(rt_arr_str_hdr, rt_string)
RT_ARR_DEFINE_ASSERT_HEADER_FN(rt_arr_str_assert_header, RT_ELEM_STR)

/// @brief Allocate a new array of string handles.
/// @details Allocates an array with @p len slots for string pointers, all
///          initialized to NULL. The array itself is reference-counted via
///          the heap allocator.
/// @param len Requested element count.
/// @return Pointer to the array payload, or NULL when allocation fails.
rt_string *rt_arr_str_alloc(size_t len)
{
    // Allocate array with RT_ELEM_STR element kind
    rt_string *arr =
        (rt_string *)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_STR, sizeof(rt_string), len, len);

    // rt_heap_alloc already zero-initializes memory, so all slots are NULL
    return arr;
}

/// @brief Release each non-null string element and free the array.
/// @details Iterates through all elements, releasing each non-null string,
///          then releases the array allocation itself.
/// @param arr Array payload pointer (may be NULL).
/// @param size Number of elements in the array.
void rt_arr_str_release(rt_string *arr, size_t size)
{
    if (!arr)
        return;

    rt_heap_hdr_t *hdr = rt_arr_str_hdr(arr);
    rt_arr_str_assert_header(hdr);

    // Release each string element
    for (size_t i = 0; i < size; ++i)
    {
        rt_str_release_maybe(arr[i]);
        arr[i] = NULL; // Clear slot after release
    }

    // Release the array itself
    rt_heap_release(arr);
}

/// @brief Read string element at index @p idx and return a retained handle.
/// @details Returns the string at @p idx after incrementing its reference count.
///          The caller must release the returned handle when done.
/// @param arr Array payload pointer; must be non-null.
/// @param idx Zero-based index within the array length.
/// @return String handle at @p idx (retained for caller), or NULL if slot is empty.
rt_string rt_arr_str_get(rt_string *arr, size_t idx)
{
    assert(arr != NULL);

    rt_heap_hdr_t *hdr = rt_arr_str_hdr(arr);
    rt_arr_str_assert_header(hdr);

    // Bounds checking (optional - could be done by caller/IL)
    assert(idx < hdr->len);

    rt_string value = arr[idx];

    // Retain before returning to caller (transfer semantics)
    rt_str_retain_maybe(value);

    return value;
}

/// @brief Write @p value to index @p idx with proper reference counting.
/// @details Retains the new value, releases the old value, then stores.
///          This maintains correct ownership semantics for the string handles.
/// @param arr Array payload pointer; must be non-null.
/// @param idx Zero-based index within the array length.
/// @param value String handle to store (may be NULL); will be retained.
void rt_arr_str_put(rt_string *arr, size_t idx, rt_string value)
{
    assert(arr != NULL);

    rt_heap_hdr_t *hdr = rt_arr_str_hdr(arr);
    rt_arr_str_assert_header(hdr);

    // Bounds checking (optional - could be done by caller/IL)
    assert(idx < hdr->len);

    // Retain new value first (in case value == arr[idx])
    rt_str_retain_maybe(value);

    // Release old value
    rt_str_release_maybe(arr[idx]);

    // Store new value
    arr[idx] = value;
}

/// @brief Query the current logical length of a string array.
/// @details Returns the element count stored in the heap header.
/// @param arr Array payload pointer.
/// @return Number of accessible elements; 0 when @p arr is NULL.
size_t rt_arr_str_len(rt_string *arr)
{
    if (!arr)
        return 0;

    rt_heap_hdr_t *hdr = rt_arr_str_hdr(arr);
    rt_arr_str_assert_header(hdr);

    return hdr->len;
}
