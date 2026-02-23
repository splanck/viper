//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_multimap.h
// Purpose: String-keyed multimap supporting multiple values per key, returning all values for a key as a Seq, with O(1) average access.
//
// Key invariants:
//   - Each key maps to a sequence of values; duplicate keys are supported.
//   - rt_multimap_get returns a Seq of all values for the key (empty Seq if absent).
//   - Values are retained when added; released when removed.
//   - Removing a key removes all its associated values.
//
// Ownership/Lifetime:
//   - MultiMap objects are heap-allocated; caller is responsible for lifetime management.
//   - String keys are copied internally. Values are retained.
//
// Links: src/runtime/collections/rt_multimap.c (implementation), src/runtime/core/rt_string.h
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
