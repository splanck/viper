//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the runtime library's dynamic array API for 64-bit float
// arrays (SINGLE/DOUBLE in BASIC). These functions mirror the rt_arr_i64_* API
// but use double elements to support floating-point values.
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

    /// @brief Resize an array to @p new_len elements with copy-on-resize semantics.
    /// @return 0 on success, -1 on allocation failure.
    int rt_arr_f64_resize(double **a_inout, size_t new_len);

    /// @brief Copy @p count elements between array payloads.
    void rt_arr_f64_copy_payload(double *dst, const double *src, size_t count);

#ifdef __cplusplus
}
#endif
