//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_dictionary.h
// Purpose: Runtime functions for string-keyed dictionary (hash map).
// Key invariants: Keys are copied (dictionary owns copies). Values are retained.
// Ownership/Lifetime: Dictionary manages its own memory. Caller manages values.
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

    /// @brief Create a new empty dictionary.
    /// @return Pointer to dictionary object.
    void *rt_dict_new(void);

    /// @brief Remove all entries from dictionary.
    /// @param dict Dictionary pointer.
    void rt_dict_clear(void *dict);

    /// @brief Get number of entries in dictionary.
    /// @param dict Dictionary pointer.
    /// @return Entry count.
    int64_t rt_dict_count(void *dict);

    /// @brief Set or update a key-value pair.
    /// @param dict Dictionary pointer.
    /// @param key String key (will be copied).
    /// @param value Object value (will be retained).
    void rt_dict_set(void *dict, rt_string key, void *value);

    /// @brief Get value for key.
    /// @param dict Dictionary pointer.
    /// @param key String key.
    /// @return Value pointer or NULL if not found.
    void *rt_dict_get(void *dict, rt_string key);

    /// @brief Check if key exists.
    /// @param dict Dictionary pointer.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int64_t rt_dict_contains_key(void *dict, rt_string key);

    /// @brief Remove entry by key.
    /// @param dict Dictionary pointer.
    /// @param key String key.
    /// @return 1 if removed, 0 if key not found.
    int64_t rt_dict_remove(void *dict, rt_string key);

#ifdef __cplusplus
}
#endif
