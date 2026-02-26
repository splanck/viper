//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_frozenset.h
// Purpose: Immutable string set created from a Seq or Bag, providing O(1) average membership
// testing with guaranteed read-only semantics after construction.
//
// Key invariants:
//   - Once created, the set cannot be modified.
//   - Membership testing is O(1) average.
//   - Constructed from a Seq or Bag of strings; source is not consumed.
//   - rt_frozenset_has returns 1 if element is present, 0 otherwise.
//
// Ownership/Lifetime:
//   - FrozenSet retains its string elements.
//   - FrozenSet objects are heap-allocated; caller is responsible for lifetime management.
//
// Links: src/runtime/collections/rt_frozenset.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a frozen set from a Seq of strings.
    /// @param items Seq containing string elements.
    /// @return Pointer to immutable set object.
    void *rt_frozenset_from_seq(void *items);

    /// @brief Create an empty frozen set.
    /// @return Pointer to empty immutable set.
    void *rt_frozenset_empty(void);

    /// @brief Get number of elements.
    /// @param obj FrozenSet pointer.
    /// @return Element count.
    int64_t rt_frozenset_len(void *obj);

    /// @brief Check if set is empty.
    /// @param obj FrozenSet pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_frozenset_is_empty(void *obj);

    /// @brief Check if element exists in set.
    /// @param obj FrozenSet pointer.
    /// @param elem String to check.
    /// @return 1 if present, 0 otherwise.
    int8_t rt_frozenset_has(void *obj, rt_string elem);

    /// @brief Get all elements as a Seq.
    /// @param obj FrozenSet pointer.
    /// @return New Seq containing all string elements.
    void *rt_frozenset_items(void *obj);

    /// @brief Create union of two frozen sets.
    /// @param obj First FrozenSet.
    /// @param other Second FrozenSet.
    /// @return New FrozenSet containing elements from both.
    void *rt_frozenset_union(void *obj, void *other);

    /// @brief Create intersection of two frozen sets.
    /// @param obj First FrozenSet.
    /// @param other Second FrozenSet.
    /// @return New FrozenSet containing elements in both.
    void *rt_frozenset_intersect(void *obj, void *other);

    /// @brief Create difference of two frozen sets.
    /// @param obj First FrozenSet.
    /// @param other Second FrozenSet.
    /// @return New FrozenSet with elements in first but not second.
    void *rt_frozenset_diff(void *obj, void *other);

    /// @brief Check if this set is a subset of another.
    /// @param obj FrozenSet pointer.
    /// @param other Other FrozenSet.
    /// @return 1 if all elements of obj are in other.
    int8_t rt_frozenset_is_subset(void *obj, void *other);

    /// @brief Check if two frozen sets are equal (same elements).
    /// @param obj First FrozenSet.
    /// @param other Second FrozenSet.
    /// @return 1 if equal, 0 otherwise.
    int8_t rt_frozenset_equals(void *obj, void *other);

#ifdef __cplusplus
}
#endif
