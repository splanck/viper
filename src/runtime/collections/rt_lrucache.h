//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_lrucache.h
// Purpose: Runtime functions for a string-keyed LRU (Least Recently Used) cache.
// Key invariants: Keys are copied (cache owns copies). Values are retained.
//                 Evicts least-recently-used entries when capacity is exceeded.
// Ownership/Lifetime: Cache manages its own memory. Caller manages values.
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

    /// @brief Create a new LRU cache with the given maximum capacity.
    /// @param capacity Maximum number of entries (must be > 0).
    /// @return Pointer to cache object.
    void *rt_lrucache_new(int64_t capacity);

    /// @brief Get number of entries currently in the cache.
    /// @param obj Cache pointer.
    /// @return Entry count.
    int64_t rt_lrucache_len(void *obj);

    /// @brief Get the maximum capacity of the cache.
    /// @param obj Cache pointer.
    /// @return Maximum capacity.
    int64_t rt_lrucache_cap(void *obj);

    /// @brief Check if cache is empty.
    /// @param obj Cache pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_lrucache_is_empty(void *obj);

    /// @brief Insert or update a key-value pair. Promotes to most-recently-used.
    /// @param obj Cache pointer.
    /// @param key String key (will be copied).
    /// @param value Object value (will be retained).
    /// @note If cache is at capacity and key is new, evicts the LRU entry.
    void rt_lrucache_put(void *obj, rt_string key, void *value);

    /// @brief Get value for key. Promotes the entry to most-recently-used.
    /// @param obj Cache pointer.
    /// @param key String key.
    /// @return Value pointer or NULL if not found.
    void *rt_lrucache_get(void *obj, rt_string key);

    /// @brief Get value for key without promoting it in the LRU order.
    /// @param obj Cache pointer.
    /// @param key String key.
    /// @return Value pointer or NULL if not found.
    void *rt_lrucache_peek(void *obj, rt_string key);

    /// @brief Check if key exists in the cache.
    /// @param obj Cache pointer.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_lrucache_has(void *obj, rt_string key);

    /// @brief Remove entry by key.
    /// @param obj Cache pointer.
    /// @param key String key.
    /// @return 1 if removed, 0 if key not found.
    int8_t rt_lrucache_remove(void *obj, rt_string key);

    /// @brief Remove the least-recently-used entry.
    /// @param obj Cache pointer.
    /// @return 1 if an entry was removed, 0 if cache was empty.
    int8_t rt_lrucache_remove_oldest(void *obj);

    /// @brief Remove all entries from cache.
    /// @param obj Cache pointer.
    void rt_lrucache_clear(void *obj);

    /// @brief Get all keys as a Seq, ordered from most to least recently used.
    /// @param obj Cache pointer.
    /// @return New Seq containing all keys as strings.
    void *rt_lrucache_keys(void *obj);

    /// @brief Get all values as a Seq, ordered from most to least recently used.
    /// @param obj Cache pointer.
    /// @return New Seq containing all values.
    void *rt_lrucache_values(void *obj);

#ifdef __cplusplus
}
#endif
