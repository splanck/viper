//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_sparsearray.h
// Purpose: Memory-efficient sparse array (only allocates for non-null slots).
// Key invariants: O(1) average get/set via hash map. Indices are arbitrary i64.
// Ownership/Lifetime: Retains stored values. Created empty.
// Links: docs/viperlib.md
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
