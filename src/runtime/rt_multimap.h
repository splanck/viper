//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_multimap.h
// Purpose: Runtime functions for a string-keyed multimap (one key -> many values).
// Key invariants: Keys are copied. Values are retained. Each key maps to a Seq.
// Ownership/Lifetime: MultiMap manages its own memory.
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

    /// @brief Create a new empty MultiMap.
    /// @return Pointer to MultiMap object.
    void *rt_multimap_new(void);

    /// @brief Get total number of values across all keys.
    /// @param obj MultiMap pointer.
    /// @return Total value count.
    int64_t rt_multimap_len(void *obj);

    /// @brief Get number of distinct keys.
    /// @param obj MultiMap pointer.
    /// @return Key count.
    int64_t rt_multimap_key_count(void *obj);

    /// @brief Check if multimap is empty.
    /// @param obj MultiMap pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_multimap_is_empty(void *obj);

    /// @brief Add a value to the given key's list.
    /// @param obj MultiMap pointer.
    /// @param key String key (will be copied if new).
    /// @param value Object value (will be retained).
    void rt_multimap_put(void *obj, rt_string key, void *value);

    /// @brief Get all values for a key as a Seq.
    /// @param obj MultiMap pointer.
    /// @param key String key.
    /// @return Seq of values (empty Seq if key not found).
    void *rt_multimap_get(void *obj, rt_string key);

    /// @brief Get the first value for a key.
    /// @param obj MultiMap pointer.
    /// @param key String key.
    /// @return First value or NULL if key not found.
    void *rt_multimap_get_first(void *obj, rt_string key);

    /// @brief Check if key exists.
    /// @param obj MultiMap pointer.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_multimap_has(void *obj, rt_string key);

    /// @brief Get count of values for a key.
    /// @param obj MultiMap pointer.
    /// @param key String key.
    /// @return Number of values for that key.
    int64_t rt_multimap_count_for(void *obj, rt_string key);

    /// @brief Remove all values for a key.
    /// @param obj MultiMap pointer.
    /// @param key String key.
    /// @return 1 if key existed and was removed, 0 if not found.
    int8_t rt_multimap_remove_all(void *obj, rt_string key);

    /// @brief Remove all entries from multimap.
    /// @param obj MultiMap pointer.
    void rt_multimap_clear(void *obj);

    /// @brief Get all keys as a Seq.
    /// @param obj MultiMap pointer.
    /// @return New Seq containing all distinct keys.
    void *rt_multimap_keys(void *obj);

#ifdef __cplusplus
}
#endif
