//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_weakmap.h
// Purpose: Map with weak value references. Values may become NULL when
//          their referent is collected. Uses string keys.
// Key invariants: String-keyed. Values stored without retaining.
//                 Getting a collected value returns NULL.
// Ownership/Lifetime: Caller manages map lifetime.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty weak map.
    /// @return New weak map object.
    void *rt_weakmap_new(void);

    /// @brief Get the number of entries (including potentially collected ones).
    /// @param map Weak map.
    /// @return Number of entries.
    int64_t rt_weakmap_len(void *map);

    /// @brief Check if the map is empty.
    /// @param map Weak map.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_weakmap_is_empty(void *map);

    /// @brief Set a value in the map (weak reference).
    /// @param map Weak map.
    /// @param key String key.
    /// @param value Value (stored as weak reference).
    void rt_weakmap_set(void *map, rt_string key, void *value);

    /// @brief Get a value from the map.
    /// @param map Weak map.
    /// @param key String key.
    /// @return Value, or NULL if not found or collected.
    void *rt_weakmap_get(void *map, rt_string key);

    /// @brief Check if a key exists in the map.
    /// @param map Weak map.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_weakmap_has(void *map, rt_string key);

    /// @brief Remove a key from the map.
    /// @param map Weak map.
    /// @param key String key.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_weakmap_remove(void *map, rt_string key);

    /// @brief Get all keys currently in the map.
    /// @param map Weak map.
    /// @return Seq of string keys.
    void *rt_weakmap_keys(void *map);

    /// @brief Remove all entries from the map.
    /// @param map Weak map.
    void rt_weakmap_clear(void *map);

    /// @brief Compact the map by removing entries with NULL values.
    /// @param map Weak map.
    /// @return Number of entries removed.
    int64_t rt_weakmap_compact(void *map);

#ifdef __cplusplus
}
#endif
