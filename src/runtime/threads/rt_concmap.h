//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_concmap.h
// Purpose: Thread-safe concurrent hash map with string keys.
// Key invariants: Mutex-protected, FNV-1a hash, separate chaining.
// Ownership/Lifetime: Keys are copied. Values are retained.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty concurrent map.
    /// @return Pointer to ConcurrentMap object.
    void *rt_concmap_new(void);

    /// @brief Get approximate number of entries.
    /// @param obj ConcurrentMap pointer.
    /// @return Entry count (approximate under concurrency).
    int64_t rt_concmap_len(void *obj);

    /// @brief Check if map is approximately empty.
    /// @param obj ConcurrentMap pointer.
    /// @return 1 if likely empty, 0 otherwise.
    int8_t rt_concmap_is_empty(void *obj);

    /// @brief Set a key-value pair (thread-safe).
    /// @param obj ConcurrentMap pointer.
    /// @param key String key (copied).
    /// @param value Value to store (retained).
    void rt_concmap_set(void *obj, rt_string key, void *value);

    /// @brief Get value by key (thread-safe).
    /// @param obj ConcurrentMap pointer.
    /// @param key String key to look up.
    /// @return Value or NULL if not found.
    void *rt_concmap_get(void *obj, rt_string key);

    /// @brief Get value with default (thread-safe).
    /// @param obj ConcurrentMap pointer.
    /// @param key String key to look up.
    /// @param default_value Value to return if key not found.
    /// @return Value for key, or default_value.
    void *rt_concmap_get_or(void *obj, rt_string key, void *default_value);

    /// @brief Check if key exists (thread-safe).
    /// @param obj ConcurrentMap pointer.
    /// @param key String key to check.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_concmap_has(void *obj, rt_string key);

    /// @brief Set key only if it doesn't exist (thread-safe, atomic).
    /// @param obj ConcurrentMap pointer.
    /// @param key String key.
    /// @param value Value to store if key is absent.
    /// @return 1 if inserted, 0 if key already existed.
    int8_t rt_concmap_set_if_missing(void *obj, rt_string key, void *value);

    /// @brief Remove a key-value pair (thread-safe).
    /// @param obj ConcurrentMap pointer.
    /// @param key String key to remove.
    /// @return 1 if removed, 0 if key not found.
    int8_t rt_concmap_remove(void *obj, rt_string key);

    /// @brief Remove all entries (thread-safe).
    /// @param obj ConcurrentMap pointer.
    void rt_concmap_clear(void *obj);

    /// @brief Get all keys as a Seq (thread-safe snapshot).
    /// @param obj ConcurrentMap pointer.
    /// @return New Seq containing all current keys.
    void *rt_concmap_keys(void *obj);

    /// @brief Get all values as a Seq (thread-safe snapshot).
    /// @param obj ConcurrentMap pointer.
    /// @return New Seq containing all current values.
    void *rt_concmap_values(void *obj);

#ifdef __cplusplus
}
#endif
