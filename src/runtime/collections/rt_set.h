//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_set.h
// Purpose: Generic hash set using object identity (pointer equality) for element comparison,
// providing O(1) average add, remove, and contains operations.
//
// Key invariants:
//   - Elements are compared by pointer identity, not deep equality.
//   - Storing the same object pointer twice is idempotent.
//   - Elements are retained when added and released when removed.
//   - rt_set_has returns 1 if the pointer is present, 0 otherwise.
//
// Ownership/Lifetime:
//   - Set objects are heap-allocated; caller is responsible for lifetime management.
//   - Elements are retained by the set while stored.
//
// Links: src/runtime/collections/rt_set.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty set.
    /// @return Pointer to set object.
    void *rt_set_new(void);

    /// @brief Get number of elements in set.
    /// @param obj Set pointer.
    /// @return Element count.
    int64_t rt_set_len(void *obj);

    /// @brief Check if set is empty.
    /// @param obj Set pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_set_is_empty(void *obj);

    /// @brief Add an element to the set.
    /// @param obj Set pointer.
    /// @param elem Element to add (retained if new).
    /// @return 1 if element was new (added), 0 if already present.
    int8_t rt_set_put(void *obj, void *elem);

    /// @brief Remove an element from the set.
    /// @param obj Set pointer.
    /// @param elem Element to remove.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_set_drop(void *obj, void *elem);

    /// @brief Check if element exists in set.
    /// @param obj Set pointer.
    /// @param elem Element to check.
    /// @return 1 if present, 0 otherwise.
    int8_t rt_set_has(void *obj, void *elem);

    /// @brief Remove all elements from set.
    /// @param obj Set pointer.
    void rt_set_clear(void *obj);

    /// @brief Get all elements as a Seq.
    /// @param obj Set pointer.
    /// @return New Seq containing all elements.
    void *rt_set_items(void *obj);

    /// @brief Create union of two sets.
    /// @param obj First set pointer.
    /// @param other Second set pointer.
    /// @return New set containing elements from both.
    void *rt_set_union(void *obj, void *other);

    /// @brief Create intersection of two sets.
    /// @param obj First set pointer.
    /// @param other Second set pointer.
    /// @return New set containing elements in both.
    void *rt_set_intersect(void *obj, void *other);

    /// @brief Create difference of two sets.
    /// @param obj First set pointer.
    /// @param other Second set pointer.
    /// @return New set containing elements in first but not second.
    void *rt_set_diff(void *obj, void *other);

    /// @brief Check if this set is a subset of another.
    /// @param obj Set pointer.
    /// @param other Other set pointer.
    /// @return 1 if all elements of obj are in other, 0 otherwise.
    int8_t rt_set_is_subset(void *obj, void *other);

    /// @brief Check if this set is a superset of another.
    /// @param obj Set pointer.
    /// @param other Other set pointer.
    /// @return 1 if all elements of other are in obj, 0 otherwise.
    int8_t rt_set_is_superset(void *obj, void *other);

    /// @brief Check if two sets are disjoint (no common elements).
    /// @param obj Set pointer.
    /// @param other Other set pointer.
    /// @return 1 if no elements in common, 0 otherwise.
    int8_t rt_set_is_disjoint(void *obj, void *other);

#ifdef __cplusplus
}
#endif
