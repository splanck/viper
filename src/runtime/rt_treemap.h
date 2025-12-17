//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_treemap.h
// Purpose: Sorted key-value map with string keys, ordered by key comparison.
// Key invariants: Keys are maintained in sorted order; Floor/Ceil/First/Last
//                 provide ordered access; all operations maintain sort order.
// Ownership/Lifetime: TreeMap objects are heap-allocated; caller responsible
//                     for lifetime management.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty sorted map.
    /// @return Pointer to new TreeMap object.
    void *rt_treemap_new(void);

    /// @brief Get the number of entries in the map.
    /// @param obj TreeMap pointer.
    /// @return Number of key-value pairs.
    int64_t rt_treemap_len(void *obj);

    /// @brief Check if the map is empty.
    /// @param obj TreeMap pointer.
    /// @return True if map contains no entries.
    int8_t rt_treemap_is_empty(void *obj);

    /// @brief Set a key-value pair (insert or update).
    /// @param obj TreeMap pointer.
    /// @param key String key.
    /// @param value Value to store.
    void rt_treemap_set(void *obj, rt_string key, void *value);

    /// @brief Get the value for a key.
    /// @param obj TreeMap pointer.
    /// @param key String key.
    /// @return Value or NULL if key not found.
    void *rt_treemap_get(void *obj, rt_string key);

    /// @brief Check if a key exists in the map.
    /// @param obj TreeMap pointer.
    /// @param key String key.
    /// @return True if key exists.
    int8_t rt_treemap_has(void *obj, rt_string key);

    /// @brief Remove a key-value pair.
    /// @param obj TreeMap pointer.
    /// @param key String key.
    /// @return True if key was found and removed.
    int8_t rt_treemap_drop(void *obj, rt_string key);

    /// @brief Remove all entries from the map.
    /// @param obj TreeMap pointer.
    void rt_treemap_clear(void *obj);

    /// @brief Get all keys as a Seq in sorted order.
    /// @param obj TreeMap pointer.
    /// @return Seq containing all keys (sorted).
    void *rt_treemap_keys(void *obj);

    /// @brief Get all values as a Seq in key-sorted order.
    /// @param obj TreeMap pointer.
    /// @return Seq containing all values.
    void *rt_treemap_values(void *obj);

    /// @brief Get the smallest (first) key.
    /// @param obj TreeMap pointer.
    /// @return First key or empty string if map is empty.
    rt_string rt_treemap_first(void *obj);

    /// @brief Get the largest (last) key.
    /// @param obj TreeMap pointer.
    /// @return Last key or empty string if map is empty.
    rt_string rt_treemap_last(void *obj);

    /// @brief Get the largest key less than or equal to the given key.
    /// @param obj TreeMap pointer.
    /// @param key Reference key.
    /// @return Floor key or empty string if no such key exists.
    rt_string rt_treemap_floor(void *obj, rt_string key);

    /// @brief Get the smallest key greater than or equal to the given key.
    /// @param obj TreeMap pointer.
    /// @param key Reference key.
    /// @return Ceiling key or empty string if no such key exists.
    rt_string rt_treemap_ceil(void *obj, rt_string key);

#ifdef __cplusplus
}
#endif
