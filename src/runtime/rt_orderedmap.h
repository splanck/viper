//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_orderedmap.h
// Purpose: Insertion-order preserving string-keyed map.
// Key invariants: Iteration order matches insertion order.
// Ownership/Lifetime: Map objects are GC-managed; values are retained.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty ordered map.
    /// @return Ordered map object.
    void *rt_orderedmap_new(void);

    /// @brief Get the number of entries.
    /// @param map Ordered map object.
    /// @return Number of entries.
    int64_t rt_orderedmap_len(void *map);

    /// @brief Check if the map is empty.
    /// @param map Ordered map object.
    /// @return 1 if empty, 0 otherwise.
    int64_t rt_orderedmap_is_empty(void *map);

    /// @brief Set a key-value pair. Preserves insertion order for new keys.
    /// @param map Ordered map object.
    /// @param key Key string.
    /// @param value Value (retained).
    void rt_orderedmap_set(void *map, rt_string key, void *value);

    /// @brief Get a value by key.
    /// @param map Ordered map object.
    /// @param key Key string.
    /// @return Value, or NULL if not found.
    void *rt_orderedmap_get(void *map, rt_string key);

    /// @brief Check if a key exists.
    /// @param map Ordered map object.
    /// @param key Key string.
    /// @return 1 if key exists, 0 otherwise.
    int64_t rt_orderedmap_has(void *map, rt_string key);

    /// @brief Remove a key-value pair.
    /// @param map Ordered map object.
    /// @param key Key string.
    /// @return 1 if removed, 0 if key not found.
    int64_t rt_orderedmap_remove(void *map, rt_string key);

    /// @brief Get all keys in insertion order as a Seq.
    /// @param map Ordered map object.
    /// @return Seq of key strings.
    void *rt_orderedmap_keys(void *map);

    /// @brief Get all values in insertion order as a Seq.
    /// @param map Ordered map object.
    /// @return Seq of values.
    void *rt_orderedmap_values(void *map);

    /// @brief Get the key at a given index (insertion order).
    /// @param map Ordered map object.
    /// @param index Zero-based index.
    /// @return Key string at index, or NULL if out of range.
    rt_string rt_orderedmap_key_at(void *map, int64_t index);

    /// @brief Clear all entries.
    /// @param map Ordered map object.
    void rt_orderedmap_clear(void *map);

#ifdef __cplusplus
}
#endif
