// File: src/runtime/rt_array.h
// Purpose: Declares dynamic int32 array helpers for the BASIC runtime.
// Key invariants: Array length never exceeds capacity; storage is contiguous.
// Ownership/Lifetime: Caller owns array handles and must free them with free().
// Links: docs/codemap.md
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__cplusplus)
#    define RT_ARR_NORETURN [[noreturn]]
#elif defined(_MSC_VER)
#    define RT_ARR_NORETURN __declspec(noreturn)
#else
#    define RT_ARR_NORETURN __attribute__((noreturn))
#endif

    /// @brief Allocate a new dynamic array of 32-bit integers.
    /// @param len Number of elements to allocate.
    /// @return Opaque array handle or NULL on allocation failure.
    void *rt_arr_i32_new(size_t len);

    /// @brief Query the current logical length of an array.
    /// @param arr Array handle returned by rt_arr_i32_new/rt_arr_i32_resize.
    /// @return Number of accessible elements; 0 when @p arr is NULL.
    size_t rt_arr_i32_len(const void *arr);

    /// @brief Read element at index @p idx without bounds checks.
    /// @param arr Array handle; must be non-null and in range.
    /// @param idx Zero-based index within the array length.
    /// @return Stored value at @p idx.
    int32_t rt_arr_i32_get(const void *arr, size_t idx);

    /// @brief Write @p value to index @p idx without bounds checks.
    /// @param arr Array handle; must be non-null and in range.
    /// @param idx Zero-based index within the array length.
    /// @param value Value to store.
    void rt_arr_i32_set(void *arr, size_t idx, int32_t value);

    /// @brief Resize an array to @p new_len elements.
    /// @param arr Existing array handle or NULL to allocate.
    /// @param new_len Requested logical length.
    /// @return Updated array handle with zero-initialized new slots or NULL on failure.
    void *rt_arr_i32_resize(void *arr, size_t new_len);

    /// @brief Abort execution due to an out-of-bounds access.
    /// @param idx Failing index.
    /// @param len Array length observed by the caller.
    RT_ARR_NORETURN void rt_arr_oob_panic(size_t idx, size_t len);

#ifdef __cplusplus
}
#endif

#undef RT_ARR_NORETURN

