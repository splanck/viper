//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_frozenmap.h
// Purpose: Immutable string-keyed map created from existing Map or parallel key/value Seqs, providing O(1) average lookup with guaranteed read-only semantics after construction.
//
// Key invariants:
//   - Once created, the map cannot be modified; there are no put/remove operations.
//   - Lookup is O(1) average using the same hash strategy as the mutable Map.
//   - Constructed from an existing Map or from parallel key and value Seqs.
//   - rt_frozenmap_has returns 1 if key exists, 0 otherwise.
//
// Ownership/Lifetime:
//   - FrozenMap retains its keys and values; source collections are not consumed.
//   - FrozenMap objects are heap-allocated; caller is responsible for lifetime management.
//
// Links: src/runtime/collections/rt_frozenmap.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a frozen map from parallel key and value Seqs.
    /// @param keys Seq of string keys.
    /// @param values Seq of object values.
    /// @return Pointer to immutable map object.
    void *rt_frozenmap_from_seqs(void *keys, void *values);

    /// @brief Create an empty frozen map.
    /// @return Pointer to empty immutable map.
    void *rt_frozenmap_empty(void);

    /// @brief Get number of entries.
    /// @param obj FrozenMap pointer.
    /// @return Entry count.
    int64_t rt_frozenmap_len(void *obj);

    /// @brief Check if map is empty.
    /// @param obj FrozenMap pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_frozenmap_is_empty(void *obj);

    /// @brief Get value for key.
    /// @param obj FrozenMap pointer.
    /// @param key String key.
    /// @return Value pointer or NULL if not found.
    void *rt_frozenmap_get(void *obj, rt_string key);

    /// @brief Check if key exists.
    /// @param obj FrozenMap pointer.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_frozenmap_has(void *obj, rt_string key);

    /// @brief Get all keys as a Seq.
    /// @param obj FrozenMap pointer.
    /// @return New Seq containing all keys.
    void *rt_frozenmap_keys(void *obj);

    /// @brief Get all values as a Seq.
    /// @param obj FrozenMap pointer.
    /// @return New Seq containing all values.
    void *rt_frozenmap_values(void *obj);

    /// @brief Get value for key or return a default.
    /// @param obj FrozenMap pointer.
    /// @param key String key.
    /// @param default_value Value to return if key not found.
    /// @return Value for key or default_value.
    void *rt_frozenmap_get_or(void *obj, rt_string key, void *default_value);

    /// @brief Merge two frozen maps. Second map's values win on conflict.
    /// @param obj First FrozenMap.
    /// @param other Second FrozenMap.
    /// @return New FrozenMap with entries from both.
    void *rt_frozenmap_merge(void *obj, void *other);

    /// @brief Check if two frozen maps are equal (same key-value pairs).
    /// @param obj First FrozenMap.
    /// @param other Second FrozenMap.
    /// @return 1 if equal, 0 otherwise.
    int8_t rt_frozenmap_equals(void *obj, void *other);

#ifdef __cplusplus
}
#endif
