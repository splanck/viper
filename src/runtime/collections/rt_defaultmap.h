//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_defaultmap.h
// Purpose: String-keyed map that returns a configured default value for missing keys instead of NULL, providing safe access without explicit missing-key checks.
//
// Key invariants:
//   - rt_defaultmap_get never returns NULL; it returns the configured default for missing keys.
//   - The default value is set at creation time and cannot be changed.
//   - Values are retained in the map; the default value is also retained.
//   - All operations are O(1) average-case.
//
// Ownership/Lifetime:
//   - DefaultMap objects are GC-managed opaque pointers.
//   - Values stored in the map are retained; callers should not free them while stored.
//
// Links: src/runtime/collections/rt_defaultmap.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new default map with a default value.
    /// @param default_value Value returned for missing keys.
    /// @return DefaultMap object.
    void *rt_defaultmap_new(void *default_value);

    /// @brief Get the number of entries.
    /// @param map DefaultMap object.
    /// @return Number of entries.
    int64_t rt_defaultmap_len(void *map);

    /// @brief Get value by key (returns default if missing).
    /// @param map DefaultMap object.
    /// @param key Key string.
    /// @return Value, or default if key not found.
    void *rt_defaultmap_get(void *map, rt_string key);

    /// @brief Set a key-value pair.
    /// @param map DefaultMap object.
    /// @param key Key string.
    /// @param value Value to store.
    void rt_defaultmap_set(void *map, rt_string key, void *value);

    /// @brief Check if a key exists (explicitly set, not just default).
    /// @param map DefaultMap object.
    /// @param key Key string.
    /// @return 1 if explicitly set, 0 otherwise.
    int64_t rt_defaultmap_has(void *map, rt_string key);

    /// @brief Remove a key-value pair.
    /// @param map DefaultMap object.
    /// @param key Key string.
    /// @return 1 if removed, 0 if not found.
    int64_t rt_defaultmap_remove(void *map, rt_string key);

    /// @brief Get all keys.
    /// @param map DefaultMap object.
    /// @return Seq of key strings.
    void *rt_defaultmap_keys(void *map);

    /// @brief Get the default value.
    /// @param map DefaultMap object.
    /// @return Default value.
    void *rt_defaultmap_get_default(void *map);

    /// @brief Clear all entries.
    /// @param map DefaultMap object.
    void rt_defaultmap_clear(void *map);

#ifdef __cplusplus
}
#endif
