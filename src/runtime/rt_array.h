// File: src/runtime/rt_array.h
// Purpose: Declares dynamic int32 array helpers for the BASIC runtime.
// Key invariants: Array length never exceeds capacity; storage is contiguous.
// Ownership/Lifetime: Arrays are reference-counted; retain/release manage shared ownership.
// Links: docs/codemap.md
#pragma once

#include "rt_heap.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Helper returning the heap header associated with @p payload.
    /// @param payload Array payload pointer (may be NULL).
    /// @return Heap header describing the allocation, or NULL for NULL payloads.
    rt_heap_hdr_t *rt_arr_i32_hdr(const int32_t *payload);

#if defined(__cplusplus)
#define RT_ARR_NORETURN [[noreturn]]
#elif defined(_MSC_VER)
#define RT_ARR_NORETURN __declspec(noreturn)
#else
#define RT_ARR_NORETURN __attribute__((noreturn))
#endif

    /// @brief Allocate a new dynamic array of 32-bit integers.
    /// @param len Number of elements to allocate.
    /// @return Array payload pointer or NULL on allocation failure.
    int32_t *rt_arr_i32_new(size_t len);

    /// @brief Increment the reference count for @p arr.
    /// @param arr Array payload pointer (may be NULL).
    void rt_arr_i32_retain(int32_t *arr);

    /// @brief Decrement the reference count for @p arr and free on zero.
    /// @param arr Array payload pointer (may be NULL).
    void rt_arr_i32_release(int32_t *arr);

    /// @brief Query the current logical length of an array.
    /// @param arr Array payload pointer returned by rt_arr_i32_new.
    /// @return Number of accessible elements; 0 when @p arr is NULL.
    size_t rt_arr_i32_len(int32_t *arr);

    /// @brief Query the current capacity in elements.
    /// @param arr Array payload pointer returned by rt_arr_i32_new.
    /// @return Capacity in elements; 0 when @p arr is NULL.
    size_t rt_arr_i32_cap(int32_t *arr);

    /// @brief Read element at index @p idx without bounds checks.
    /// @param arr Array payload pointer; must be non-null and in range.
    /// @param idx Zero-based index within the array length.
    /// @return Stored value at @p idx.
    int32_t rt_arr_i32_get(int32_t *arr, size_t idx);

    /// @brief Write @p value to index @p idx without bounds checks.
    /// @param arr Array payload pointer; must be non-null and in range.
    /// @param idx Zero-based index within the array length.
    /// @param value Value to store.
    void rt_arr_i32_set(int32_t *arr, size_t idx, int32_t value);

    /// @brief Resize an array to @p new_len elements with copy-on-resize semantics.
    /// @param a_inout Address of the array payload pointer (may point to NULL).
    /// @param new_len Requested logical length.
    /// @return 0 on success, -1 on allocation failure; pointer may be rebound on success.
    int rt_arr_i32_resize(int32_t **a_inout, size_t new_len);

    /// @brief Abort execution due to an out-of-bounds access.
    /// @param idx Failing index.
    /// @param len Array length observed by the caller.
    RT_ARR_NORETURN void rt_arr_oob_panic(size_t idx, size_t len);

#ifdef __cplusplus
}
#endif

#undef RT_ARR_NORETURN
