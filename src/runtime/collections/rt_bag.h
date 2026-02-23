//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_bag.h
// Purpose: String set (Bag) backed by a hash table, providing O(1) average membership testing, insertion, and removal of unique string values.
//
// Key invariants:
//   - Stores unique strings only; duplicate insertions are silently ignored.
//   - All operations are O(1) average-case using an open-addressing hash table.
//   - Iteration order is unspecified and may change after insertions.
//   - rt_bag_contains returns 1 if present, 0 if absent.
//
// Ownership/Lifetime:
//   - Bag objects are heap-allocated; caller is responsible for lifetime management.
//   - String values are copied into the bag; caller retains ownership of input strings.
//
// Links: src/runtime/collections/rt_bag.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty bag.
    /// @return Pointer to bag object.
    void *rt_bag_new(void);

    /// @brief Get number of elements in bag.
    /// @param obj Bag pointer.
    /// @return Element count.
    int64_t rt_bag_len(void *obj);

    /// @brief Check if bag is empty.
    /// @param obj Bag pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_bag_is_empty(void *obj);

    /// @brief Add a string to the bag.
    /// @param obj Bag pointer.
    /// @param str String to add (will be copied).
    /// @return 1 if string was new (added), 0 if already present.
    int8_t rt_bag_put(void *obj, rt_string str);

    /// @brief Remove a string from the bag.
    /// @param obj Bag pointer.
    /// @param str String to remove.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_bag_drop(void *obj, rt_string str);

    /// @brief Check if string exists in bag.
    /// @param obj Bag pointer.
    /// @param str String to check.
    /// @return 1 if present, 0 otherwise.
    int8_t rt_bag_has(void *obj, rt_string str);

    /// @brief Remove all elements from bag.
    /// @param obj Bag pointer.
    void rt_bag_clear(void *obj);

    /// @brief Get all elements as a Seq of strings.
    /// @param obj Bag pointer.
    /// @return New Seq containing all strings.
    void *rt_bag_items(void *obj);

    /// @brief Create union of two bags.
    /// @param obj First bag pointer.
    /// @param other Second bag pointer.
    /// @return New bag containing elements from both.
    void *rt_bag_merge(void *obj, void *other);

    /// @brief Create intersection of two bags.
    /// @param obj First bag pointer.
    /// @param other Second bag pointer.
    /// @return New bag containing elements in both.
    void *rt_bag_common(void *obj, void *other);

    /// @brief Create difference of two bags.
    /// @param obj First bag pointer.
    /// @param other Second bag pointer.
    /// @return New bag containing elements in first but not second.
    void *rt_bag_diff(void *obj, void *other);

#ifdef __cplusplus
}
#endif
