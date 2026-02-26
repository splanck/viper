//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_countmap.h
// Purpose: Frequency counting map from string keys to non-negative integer counts, supporting
// increment, decrement, get, top-N queries, and iteration over counted items.
//
// Key invariants:
//   - Counts are always >= 0; decrementing a count to 0 removes the entry.
//   - rt_countmap_get returns 0 for keys not present.
//   - All operations are O(1) average-case.
//   - rt_countmap_top_n returns up to N keys with highest counts.
//
// Ownership/Lifetime:
//   - CountMap objects are heap-allocated; caller is responsible for lifetime management.
//   - String keys are copied internally; caller retains ownership of input strings.
//
// Links: src/runtime/collections/rt_countmap.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty CountMap.
    /// @return Pointer to CountMap object.
    void *rt_countmap_new(void);

    /// @brief Get number of distinct keys.
    /// @param obj CountMap pointer.
    /// @return Number of keys with count > 0.
    int64_t rt_countmap_len(void *obj);

    /// @brief Check if countmap is empty.
    /// @param obj CountMap pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_countmap_is_empty(void *obj);

    /// @brief Increment count for key by 1.
    /// @param obj CountMap pointer.
    /// @param key String key.
    /// @return New count after increment.
    int64_t rt_countmap_inc(void *obj, rt_string key);

    /// @brief Increment count for key by a specific amount.
    /// @param obj CountMap pointer.
    /// @param key String key.
    /// @param n Amount to add (must be > 0).
    /// @return New count after increment.
    int64_t rt_countmap_inc_by(void *obj, rt_string key, int64_t n);

    /// @brief Decrement count for key by 1. Removes entry if count reaches 0.
    /// @param obj CountMap pointer.
    /// @param key String key.
    /// @return New count (0 means entry was removed).
    int64_t rt_countmap_dec(void *obj, rt_string key);

    /// @brief Get current count for key.
    /// @param obj CountMap pointer.
    /// @param key String key.
    /// @return Count for key, or 0 if not present.
    int64_t rt_countmap_get(void *obj, rt_string key);

    /// @brief Set count for key directly.
    /// @param obj CountMap pointer.
    /// @param key String key.
    /// @param count New count (0 removes the entry).
    void rt_countmap_set(void *obj, rt_string key, int64_t count);

    /// @brief Check if key has a count > 0.
    /// @param obj CountMap pointer.
    /// @param key String key.
    /// @return 1 if key exists, 0 otherwise.
    int8_t rt_countmap_has(void *obj, rt_string key);

    /// @brief Get total of all counts.
    /// @param obj CountMap pointer.
    /// @return Sum of all counts.
    int64_t rt_countmap_total(void *obj);

    /// @brief Get all keys as a Seq.
    /// @param obj CountMap pointer.
    /// @return New Seq of keys.
    void *rt_countmap_keys(void *obj);

    /// @brief Get top N keys by count (descending).
    /// @param obj CountMap pointer.
    /// @param n Maximum number of keys to return.
    /// @return New Seq of keys sorted by count descending.
    void *rt_countmap_most_common(void *obj, int64_t n);

    /// @brief Remove a key entirely.
    /// @param obj CountMap pointer.
    /// @param key String key.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_countmap_remove(void *obj, rt_string key);

    /// @brief Remove all entries.
    /// @param obj CountMap pointer.
    void rt_countmap_clear(void *obj);

#ifdef __cplusplus
}
#endif
