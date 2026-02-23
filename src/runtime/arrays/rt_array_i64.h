//===----------------------------------------------------------------------===//
//
// File: src/runtime/arrays/rt_array_i64.h
// Purpose: Dynamic array API for 64-bit integers (int64_t) supporting BASIC LONG typed collections, mirroring the i32 array interface with allocation, refcounting, bounds-checked access, and resize.
//
// Key invariants:
//   - Payload pointers are preceded by an rt_heap_hdr_t header at a negative offset.
//   - length <= capacity at all times; indexed access traps on out-of-bounds.
//   - New arrays start with refcount 1.
//   - Resize may reallocate and rebind the payload pointer.
//
// Ownership/Lifetime:
//   - Reference-counted via rt_arr_i64_retain/release.
//   - The caller owns the initial reference from rt_arr_i64_new.
//   - Resize transfers ownership of the old allocation.
//
// Links: src/runtime/arrays/rt_array_i64.c (implementation), src/runtime/core/rt_heap.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_heap.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Helper returning the heap header associated with @p payload.
    rt_heap_hdr_t *rt_arr_i64_hdr(const int64_t *payload);

    /// @brief Allocate a new dynamic array of 64-bit integers.
    /// @param len Number of elements to allocate.
    /// @return Array payload pointer or NULL on allocation failure.
    int64_t *rt_arr_i64_new(size_t len);

    /// @brief Increment the reference count for @p arr.
    void rt_arr_i64_retain(int64_t *arr);

    /// @brief Decrement the reference count for @p arr and free on zero.
    void rt_arr_i64_release(int64_t *arr);

    /// @brief Query the current logical length of an array.
    /// @return Number of accessible elements; 0 when @p arr is NULL.
    size_t rt_arr_i64_len(int64_t *arr);

    /// @brief Query the current capacity in elements.
    size_t rt_arr_i64_cap(int64_t *arr);

    /// @brief Read element at index @p idx with bounds checking.
    int64_t rt_arr_i64_get(int64_t *arr, size_t idx);

    /// @brief Write @p value to index @p idx with bounds checking.
    void rt_arr_i64_set(int64_t *arr, size_t idx, int64_t value);

    /// @brief Read element at index @p idx WITHOUT bounds checking.
    /// @warning No bounds checking! Use only when compiler has verified safety.
    static inline int64_t rt_arr_i64_get_unchecked(int64_t *arr, size_t idx)
    {
        return arr[idx];
    }

    /// @brief Write @p value to index @p idx WITHOUT bounds checking.
    /// @warning No bounds checking! Use only when compiler has verified safety.
    static inline void rt_arr_i64_set_unchecked(int64_t *arr, size_t idx, int64_t value)
    {
        arr[idx] = value;
    }

    /// @brief Resize an array to @p new_len elements with copy-on-resize semantics.
    /// @return 0 on success, -1 on allocation failure.
    int rt_arr_i64_resize(int64_t **a_inout, size_t new_len);

    /// @brief Copy @p count elements between array payloads.
    void rt_arr_i64_copy_payload(int64_t *dst, const int64_t *src, size_t count);

#ifdef __cplusplus
}
#endif
