//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_intmap.h
// Purpose: Runtime functions for integer-keyed map (hash map).
// Key invariants: Keys are int64_t stored directly. Values are retained.
// Ownership/Lifetime: Map manages its own memory. Caller manages values.
// Links: src/il/runtime/classes/RuntimeClasses.inc
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty integer-keyed map.
    /// @return Pointer to intmap object.
    void *rt_intmap_new(void);

    /// @brief Get number of entries in intmap.
    /// @param obj IntMap pointer.
    /// @return Entry count.
    int64_t rt_intmap_len(void *obj);

    /// @brief Check if intmap is empty.
    /// @param obj IntMap pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_intmap_is_empty(void *obj);

    /// @brief Set or update a key-value pair.
    /// @param obj IntMap pointer.
    /// @param key Integer key.
    /// @param value Object value (will be retained).
    void rt_intmap_set(void *obj, int64_t key, void *value);

    /// @brief Get value for key.
    /// @param obj IntMap pointer.
    /// @param key Integer key.
    /// @return Value pointer or NULL if not found.
    void *rt_intmap_get(void *obj, int64_t key);

    /// @brief Get value for key or return a default when missing.
    /// @param obj IntMap pointer.
    /// @param key Integer key.
    /// @param default_value Value to return when key is not present.
    /// @return Existing value when present; otherwise default_value.
    void *rt_intmap_get_or(void *obj, int64_t key, void *default_value);

    /// @brief Check if key exists.
    /// @param obj IntMap pointer.
    /// @param key Integer key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_intmap_has(void *obj, int64_t key);

    /// @brief Remove entry by key.
    /// @param obj IntMap pointer.
    /// @param key Integer key.
    /// @return 1 if removed, 0 if key not found.
    int8_t rt_intmap_remove(void *obj, int64_t key);

    /// @brief Remove all entries from intmap.
    /// @param obj IntMap pointer.
    void rt_intmap_clear(void *obj);

    /// @brief Get all keys as a Seq of boxed integers.
    /// @param obj IntMap pointer.
    /// @return New Seq containing all keys as boxed i64.
    void *rt_intmap_keys(void *obj);

    /// @brief Get all values as a Seq.
    /// @param obj IntMap pointer.
    /// @return New Seq containing all values.
    void *rt_intmap_values(void *obj);

#ifdef __cplusplus
}
#endif
