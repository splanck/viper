//===----------------------------------------------------------------------===//
//
// File: src/runtime/arrays/rt_array_f64.h
// Purpose: Dynamic array API for 64-bit floats (double) supporting BASIC SINGLE/DOUBLE typed collections, mirroring the i64 array interface with allocation, refcounting, bounds-checked access, and resize.
//
// Key invariants:
//   - Payload pointers are preceded by an rt_heap_hdr_t header at a negative offset.
//   - length <= capacity at all times; indexed access traps on out-of-bounds.
//   - New arrays start with refcount 1.
//   - Resize may reallocate and rebind the payload pointer.
//
// Ownership/Lifetime:
//   - Reference-counted via rt_arr_f64_retain/release.
//   - The caller owns the initial reference from rt_arr_f64_new.
//   - Resize transfers ownership of the old allocation.
//
// Links: src/runtime/arrays/rt_array_f64.c (implementation), src/runtime/core/rt_heap.h
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
    rt_heap_hdr_t *rt_arr_f64_hdr(const double *payload);

    /// @brief Allocate a new dynamic array of 64-bit floats.
    /// @param len Number of elements to allocate.
    /// @return Array payload pointer or NULL on allocation failure.
    double *rt_arr_f64_new(size_t len);

    /// @brief Increment the reference count for @p arr.
    void rt_arr_f64_retain(double *arr);

    /// @brief Decrement the reference count for @p arr and free on zero.
    void rt_arr_f64_release(double *arr);

    /// @brief Query the current logical length of an array.
    /// @return Number of accessible elements; 0 when @p arr is NULL.
    size_t rt_arr_f64_len(double *arr);

    /// @brief Query the current capacity in elements.
    size_t rt_arr_f64_cap(double *arr);

    /// @brief Read element at index @p idx with bounds checking.
    double rt_arr_f64_get(double *arr, size_t idx);

    /// @brief Write @p value to index @p idx with bounds checking.
    void rt_arr_f64_set(double *arr, size_t idx, double value);

    /// @brief Read element at index @p idx WITHOUT bounds checking.
    /// @warning No bounds checking! Use only when compiler has verified safety.
    static inline double rt_arr_f64_get_unchecked(double *arr, size_t idx)
    {
        return arr[idx];
    }

    /// @brief Write @p value to index @p idx WITHOUT bounds checking.
    /// @warning No bounds checking! Use only when compiler has verified safety.
    static inline void rt_arr_f64_set_unchecked(double *arr, size_t idx, double value)
    {
        arr[idx] = value;
    }

    /// @brief Resize an array to @p new_len elements with copy-on-resize semantics.
    /// @return 0 on success, -1 on allocation failure.
    int rt_arr_f64_resize(double **a_inout, size_t new_len);

    /// @brief Copy @p count elements between array payloads.
    void rt_arr_f64_copy_payload(double *dst, const double *src, size_t count);

#ifdef __cplusplus
}
#endif
