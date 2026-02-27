//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_map.h
// Purpose: String-keyed hash map providing O(1) average insertion, lookup, removal, and iteration
// for the Viper.Collections.Map runtime class.
//
// Key invariants:
//   - Keys are copied by the map; the map owns its key copies.
//   - Values are retained on insertion and released on removal or overwrite.
//   - All operations are O(1) average-case using open-addressing.
//   - rt_map_get returns NULL for keys not present.
//
// Ownership/Lifetime:
//   - Map objects are heap-allocated; caller is responsible for lifetime management.
//   - Values are retained while stored in the map.
//
// Links: src/runtime/collections/rt_map.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Map class identifier for heap header tagging.
#define RT_MAP_CLASS_ID 3

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

    /// @brief Get value for key or return a default when missing.
    /// @details Does not mutate the map: missing keys do not create new entries.
    /// @param obj Map pointer.
    /// @param key String key.
    /// @param default_value Value to return when @p key is not present.
    /// @return Existing value when present; otherwise @p default_value.
    void *rt_map_get_or(void *obj, rt_string key, void *default_value);

    /// @brief Check if key exists.
    /// @param obj Map pointer.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_map_has(void *obj, rt_string key);

    /// @brief Insert @p value for @p key only when the key is missing.
    /// @details When @p key is already present, leaves the map unchanged and returns 0.
    /// @param obj Map pointer.
    /// @param key String key (will be copied when inserted).
    /// @param value Object value (will be retained when inserted).
    /// @return 1 when an entry was inserted, 0 otherwise.
    int8_t rt_map_set_if_missing(void *obj, rt_string key, void *value);

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

    //=========================================================================
    // Typed Accessors (box/unbox wrappers)
    //=========================================================================

    /// @brief Set an integer value (boxes automatically).
    void rt_map_set_int(void *obj, rt_string key, int64_t value);

    /// @brief Get an integer value (unboxes, returns 0 if missing).
    int64_t rt_map_get_int(void *obj, rt_string key);

    /// @brief Get an integer value with default (unboxes, returns def if missing).
    int64_t rt_map_get_int_or(void *obj, rt_string key, int64_t def);

    /// @brief Set a float value (boxes automatically).
    void rt_map_set_float(void *obj, rt_string key, double value);

    /// @brief Get a float value (unboxes, returns 0.0 if missing).
    double rt_map_get_float(void *obj, rt_string key);

    /// @brief Get a float value with default (unboxes, returns def if missing).
    double rt_map_get_float_or(void *obj, rt_string key, double def);

    /// @brief Set a string value (wraps as object).
    void rt_map_set_str(void *obj, rt_string key, rt_string value);

    /// @brief Get a string value (returns empty string if missing).
    rt_string rt_map_get_str(void *obj, rt_string key);

#ifdef __cplusplus
}
#endif
