//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_map.h
// Purpose: Runtime functions for string-keyed map (hash map).
// Key invariants: Keys are copied (map owns copies). Values are retained.
// Ownership/Lifetime: Map manages its own memory. Caller manages values.
// Links: src/il/runtime/classes/RuntimeClasses.inc
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty map.
    /// @return Pointer to map object.
    void *rt_map_new(void);

    /// @brief Get number of entries in map.
    /// @param obj Map pointer.
    /// @return Entry count.
    int64_t rt_map_len(void *obj);

    /// @brief Check if map is empty.
    /// @param obj Map pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_map_is_empty(void *obj);

    /// @brief Set or update a key-value pair.
    /// @param obj Map pointer.
    /// @param key String key (will be copied).
    /// @param value Object value (will be retained).
    void rt_map_set(void *obj, rt_string key, void *value);

    /// @brief Get value for key.
    /// @param obj Map pointer.
    /// @param key String key.
    /// @return Value pointer or NULL if not found.
    void *rt_map_get(void *obj, rt_string key);

    /// @brief Check if key exists.
    /// @param obj Map pointer.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_map_has(void *obj, rt_string key);

    /// @brief Remove entry by key.
    /// @param obj Map pointer.
    /// @param key String key.
    /// @return 1 if removed, 0 if key not found.
    int8_t rt_map_remove(void *obj, rt_string key);

    /// @brief Remove all entries from map.
    /// @param obj Map pointer.
    void rt_map_clear(void *obj);

    /// @brief Get all keys as a Seq.
    /// @param obj Map pointer.
    /// @return New Seq containing all keys as strings.
    void *rt_map_keys(void *obj);

    /// @brief Get all values as a Seq.
    /// @param obj Map pointer.
    /// @return New Seq containing all values.
    void *rt_map_values(void *obj);

#ifdef __cplusplus
}
#endif
