//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_bimap.h
// Purpose: Bidirectional string-to-string map where every key maps to exactly one value and every value maps to exactly one key, supporting lookup in both directions.
//
// Key invariants:
//   - Every key maps to exactly one value and every value maps to exactly one key.
//   - Putting a key/value pair that conflicts with an existing mapping evicts the old entry.
//   - Lookup by key and lookup by value are both O(1) average.
//   - Keys and values must be distinct non-NULL strings.
//
// Ownership/Lifetime:
//   - BiMap objects are heap-allocated; caller is responsible for lifetime management.
//   - String keys and values are copied internally.
//
// Links: src/runtime/collections/rt_bimap.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty BiMap.
    /// @return Pointer to BiMap object.
    void *rt_bimap_new(void);

    /// @brief Get current number of entries.
    /// @param obj BiMap pointer.
    /// @return Entry count.
    int64_t rt_bimap_len(void *obj);

    /// @brief Check if bimap is empty.
    /// @param obj BiMap pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_bimap_is_empty(void *obj);

    /// @brief Insert a bidirectional key<->value mapping.
    /// @details If key already exists, the old value mapping is removed.
    ///          If value already exists under a different key, that old key
    ///          mapping is removed. This ensures strict 1:1 relationship.
    /// @param obj BiMap pointer.
    /// @param key Key string.
    /// @param value Value string.
    void rt_bimap_put(void *obj, rt_string key, rt_string value);

    /// @brief Look up value by key.
    /// @param obj BiMap pointer.
    /// @param key Key string.
    /// @return Value string, or empty string if not found.
    rt_string rt_bimap_get_by_key(void *obj, rt_string key);

    /// @brief Look up key by value (inverse lookup).
    /// @param obj BiMap pointer.
    /// @param value Value string.
    /// @return Key string, or empty string if not found.
    rt_string rt_bimap_get_by_value(void *obj, rt_string value);

    /// @brief Check if key exists.
    /// @param obj BiMap pointer.
    /// @param key Key string.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_bimap_has_key(void *obj, rt_string key);

    /// @brief Check if value exists.
    /// @param obj BiMap pointer.
    /// @param value Value string.
    /// @return 1 if value exists, 0 otherwise.
    int8_t rt_bimap_has_value(void *obj, rt_string value);

    /// @brief Remove mapping by key.
    /// @param obj BiMap pointer.
    /// @param key Key string.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_bimap_remove_by_key(void *obj, rt_string key);

    /// @brief Remove mapping by value.
    /// @param obj BiMap pointer.
    /// @param value Value string.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_bimap_remove_by_value(void *obj, rt_string value);

    /// @brief Get all keys as a Seq.
    /// @param obj BiMap pointer.
    /// @return New Seq containing all keys.
    void *rt_bimap_keys(void *obj);

    /// @brief Get all values as a Seq.
    /// @param obj BiMap pointer.
    /// @return New Seq containing all values.
    void *rt_bimap_values(void *obj);

    /// @brief Remove all entries.
    /// @param obj BiMap pointer.
    void rt_bimap_clear(void *obj);

#ifdef __cplusplus
}
#endif
