//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_trie.h
// Purpose: Prefix tree (Trie) for string keys supporting insertion, lookup, prefix search, and
// auto-complete queries with O(k) operations where k is the key length.
//
// Key invariants:
//   - Keys are stored as sequences of ASCII characters.
//   - Prefix search returns all keys with the given prefix.
//   - rt_trie_contains returns 1 only for exact key matches, not just prefixes.
//   - Values are retained while stored in the trie.
//
// Ownership/Lifetime:
//   - Trie objects are heap-allocated; caller is responsible for lifetime management.
//   - String keys are used as-is; internal nodes store individual characters.
//
// Links: src/runtime/collections/rt_trie.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty Trie.
    /// @return Pointer to Trie object.
    void *rt_trie_new(void);

    /// @brief Get number of keys in the Trie.
    /// @param obj Trie pointer.
    /// @return Key count.
    int64_t rt_trie_len(void *obj);

    /// @brief Check if Trie is empty.
    /// @param obj Trie pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_trie_is_empty(void *obj);

    /// @brief Insert a key-value pair.
    /// @param obj Trie pointer.
    /// @param key String key.
    /// @param value Object value (will be retained).
    void rt_trie_put(void *obj, rt_string key, void *value);

    /// @brief Get value for exact key match.
    /// @param obj Trie pointer.
    /// @param key String key.
    /// @return Value pointer or NULL if not found.
    void *rt_trie_get(void *obj, rt_string key);

    /// @brief Check if exact key exists.
    /// @param obj Trie pointer.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_trie_has(void *obj, rt_string key);

    /// @brief Check if any keys start with the given prefix.
    /// @param obj Trie pointer.
    /// @param prefix String prefix.
    /// @return 1 if any keys have this prefix, 0 otherwise.
    int8_t rt_trie_has_prefix(void *obj, rt_string prefix);

    /// @brief Get all keys that start with the given prefix.
    /// @param obj Trie pointer.
    /// @param prefix String prefix.
    /// @return Seq of matching keys.
    void *rt_trie_with_prefix(void *obj, rt_string prefix);

    /// @brief Find the longest key that is a prefix of the given string.
    /// @param obj Trie pointer.
    /// @param str String to match against.
    /// @return Longest matching prefix key, or empty string if none.
    rt_string rt_trie_longest_prefix(void *obj, rt_string str);

    /// @brief Remove a key.
    /// @param obj Trie pointer.
    /// @param key String key.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_trie_remove(void *obj, rt_string key);

    /// @brief Remove all entries.
    /// @param obj Trie pointer.
    void rt_trie_clear(void *obj);

    /// @brief Get all keys as a Seq.
    /// @param obj Trie pointer.
    /// @return Seq of all keys (sorted lexicographically).
    void *rt_trie_keys(void *obj);

#ifdef __cplusplus
}
#endif
