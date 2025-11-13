//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares dynamic string array operations for the BASIC runtime,
// providing specialized array implementations for string-typed collections.
// String arrays have unique ownership semantics where both the array container
// and individual string elements are independently reference-counted.
//
// BASIC's string arrays (DIM names$(100)) store references to string objects,
// not the strings themselves. Each array element is a pointer to a reference-counted
// string. The array itself is also reference-counted, enabling arrays to be
// passed by reference without copying. This creates a two-level reference counting
// scheme: the array container and the element strings.
//
// Memory Management Model:
// - Array allocation: Creates a new array container with specified capacity,
//   all slots initialized to NULL
// - Element assignment: rt_arr_str_put releases the old element (if any),
//   retains the new element, and stores the new reference
// - Element access: rt_arr_str_get returns a retained reference that caller
//   must release when done
// - Array destruction: rt_arr_str_release releases all non-null elements
//   before freeing the array container
//
// This design ensures proper cleanup of string resources while supporting
// efficient sharing and assignment patterns common in BASIC programs.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_heap.h"
#include "rt_string.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Allocate a new dynamic array of string handles.
    /// @details Allocates an array of @p len string pointers, all initialized to NULL.
    ///          Callers must use rt_arr_str_put to assign values, which handles retain/release.
    /// @param len Number of string slots to allocate.
    /// @return Array payload pointer or NULL on allocation failure.
    rt_string *rt_arr_str_alloc(size_t len);

    /// @brief Release each non-null string element and free the array.
    /// @details Iterates through all @p size elements, calling rt_str_release_maybe on each,
    ///          then releases the array itself via the heap allocator.
    /// @param arr Array payload pointer (may be NULL).
    /// @param size Number of elements in the array (logical length).
    void rt_arr_str_release(rt_string *arr, size_t size);

    /// @brief Read string element at index @p idx and return a retained handle.
    /// @details Returns the string at @p idx after incrementing its reference count.
    ///          Caller must call rt_str_release_maybe on the returned handle when done.
    /// @param arr Array payload pointer; must be non-null and in range.
    /// @param idx Zero-based index within the array length.
    /// @return String handle at @p idx (retained for caller), or NULL if slot is empty.
    rt_string rt_arr_str_get(rt_string *arr, size_t idx);

    /// @brief Write @p value to index @p idx with proper reference counting.
    /// @details Retains the new @p value, releases the old value at @p idx,
    ///          then stores the new value. This maintains proper ownership semantics.
    /// @param arr Array payload pointer; must be non-null and in range.
    /// @param idx Zero-based index within the array length.
    /// @param value String handle to store (may be NULL); will be retained.
    void rt_arr_str_put(rt_string *arr, size_t idx, rt_string value);

    /// @brief Query the current logical length of a string array.
    /// @details Returns the element count stored in the heap header.
    /// @param arr Array payload pointer returned by rt_arr_str_alloc.
    /// @return Number of accessible elements; 0 when @p arr is NULL.
    size_t rt_arr_str_len(rt_string *arr);

#ifdef __cplusplus
}
#endif
