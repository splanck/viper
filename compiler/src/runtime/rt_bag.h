//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_bag.h
// Purpose: Runtime functions for string set (Bag) using hash table.
// Key invariants: Stores unique strings only. All operations are O(1) average.
// Ownership/Lifetime: Bag manages its own memory. Strings are copied.
// Links: docs/viperlib.md
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
