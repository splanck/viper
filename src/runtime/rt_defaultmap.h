//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_defaultmap.h
// Purpose: Map with a default value returned for missing keys.
// Key invariants: Get never returns NULL (returns default instead).
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
