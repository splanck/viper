//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_weakmap.h
// Purpose: String-keyed map holding weak (non-retaining) references to values, so values may be collected; getting a collected value returns NULL.
//
// Key invariants:
//   - Values are stored without retaining; the map does not prevent collection.
//   - rt_weakmap_get returns NULL for both missing keys and collected values.
//   - String keys are copied; the map owns its key copies.
//   - Iterating a weakmap may skip entries whose values were collected.
//
// Ownership/Lifetime:
//   - Caller manages weakmap lifetime; no reference counting on the map itself.
//   - Values must not be freed while the weakmap holds a pointer to them unless the caller accounts for it.
//
// Links: src/runtime/collections/rt_weakmap.c (implementation), src/runtime/core/rt_string.h
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
