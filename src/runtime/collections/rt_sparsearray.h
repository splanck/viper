//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_sparsearray.h
// Purpose: Memory-efficient sparse array mapping arbitrary int64_t indices to object pointer values, allocating storage only for non-null entries via a hash map.
//
// Key invariants:
//   - Indices are arbitrary signed 64-bit integers; no range restriction.
//   - Only non-null entries consume memory.
//   - All operations are O(1) average-case via internal hash map.
//   - rt_sparse_get returns NULL for indices not explicitly set.
//
// Ownership/Lifetime:
//   - SparseArray objects are heap-allocated; caller is responsible for lifetime management.
//   - Stored values are retained while in the array.
//
// Links: src/runtime/collections/rt_sparsearray.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty sparse array.
    /// @return Pointer to sparse array object.
    void *rt_sparse_new(void);

    /// @brief Get number of non-null entries.
    /// @param obj SparseArray pointer.
    /// @return Entry count.
    int64_t rt_sparse_len(void *obj);

    /// @brief Get value at index, or NULL if not set.
    /// @param obj SparseArray pointer.
    /// @param index Arbitrary integer index.
    /// @return Value at index or NULL.
    void *rt_sparse_get(void *obj, int64_t index);

    /// @brief Set value at index.
    /// @param obj SparseArray pointer.
    /// @param index Arbitrary integer index.
    /// @param value Value to store (retained).
    void rt_sparse_set(void *obj, int64_t index, void *value);

    /// @brief Check if index has a value.
    /// @param obj SparseArray pointer.
    /// @param index Index to check.
    /// @return 1 if set, 0 otherwise.
    int8_t rt_sparse_has(void *obj, int64_t index);

    /// @brief Remove value at index.
    /// @param obj SparseArray pointer.
    /// @param index Index to remove.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_sparse_remove(void *obj, int64_t index);

    /// @brief Get all indices that have values.
    /// @param obj SparseArray pointer.
    /// @return New Seq of i64 indices.
    void *rt_sparse_indices(void *obj);

    /// @brief Get all values.
    /// @param obj SparseArray pointer.
    /// @return New Seq of values.
    void *rt_sparse_values(void *obj);

    /// @brief Remove all entries.
    /// @param obj SparseArray pointer.
    void rt_sparse_clear(void *obj);

#ifdef __cplusplus
}
#endif
