//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_iter.h
// Purpose: Unified stateful iterator protocol for all collection types, providing a common
// next/has_next interface over Seq, List, Deque, Map, Set, Stack, Queue, Ring, and Trie.
//
// Key invariants:
//   - Iterators are lightweight handles wrapping a collection reference and a position.
//   - Collections must remain valid and unmodified during iteration.
//   - Modifying a collection during iteration yields undefined results.
//   - rt_iter_has_next returns 1 while elements remain; 0 when exhausted.
//
// Ownership/Lifetime:
//   - Iterator objects are heap-allocated; caller must free after use.
//   - Iterators hold a borrowed reference to the underlying collection.
//
// Links: src/runtime/collections/rt_iter.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create an iterator over a Seq.
    /// @param seq Seq object.
    /// @return Iterator handle, or NULL if seq is NULL.
    void *rt_iter_from_seq(void *seq);

    /// @brief Create an iterator over a List.
    /// @param list List object.
    /// @return Iterator handle.
    void *rt_iter_from_list(void *list);

    /// @brief Create an iterator over a Deque.
    /// @param deque Deque object.
    /// @return Iterator handle.
    void *rt_iter_from_deque(void *deque);

    /// @brief Create an iterator over a Map's keys.
    /// @param map Map object.
    /// @return Iterator handle producing key strings.
    void *rt_iter_from_map_keys(void *map);

    /// @brief Create an iterator over a Map's values.
    /// @param map Map object.
    /// @return Iterator handle producing value objects.
    void *rt_iter_from_map_values(void *map);

    /// @brief Create an iterator over a Set's items.
    /// @param set Set object.
    /// @return Iterator handle producing items.
    void *rt_iter_from_set(void *set);

    /// @brief Create an iterator from a Stack (top to bottom).
    /// @param stack Stack object.
    /// @return Iterator handle.
    void *rt_iter_from_stack(void *stack);

    /// @brief Create an iterator from a Ring buffer.
    /// @param ring Ring object.
    /// @return Iterator handle.
    void *rt_iter_from_ring(void *ring);

    /// @brief Check if more elements are available.
    /// @param iter Iterator handle.
    /// @return 1 if next() will succeed, 0 if exhausted.
    int8_t rt_iter_has_next(void *iter);

    /// @brief Get the current element and advance the iterator.
    /// @param iter Iterator handle.
    /// @return Current element, or NULL if exhausted.
    void *rt_iter_next(void *iter);

    /// @brief Peek at the current element without advancing.
    /// @param iter Iterator handle.
    /// @return Current element, or NULL if exhausted.
    void *rt_iter_peek(void *iter);

    /// @brief Reset the iterator to the beginning.
    /// @param iter Iterator handle.
    void rt_iter_reset(void *iter);

    /// @brief Get the current zero-based position.
    /// @param iter Iterator handle.
    /// @return Current index (0 before first next(), increments with each next()).
    int64_t rt_iter_index(void *iter);

    /// @brief Get the total number of elements in the underlying collection.
    /// @param iter Iterator handle.
    /// @return Collection size, or 0 if NULL.
    int64_t rt_iter_count(void *iter);

    /// @brief Collect remaining elements into a new Seq.
    /// @param iter Iterator handle.
    /// @return New Seq containing the remaining elements.
    void *rt_iter_to_seq(void *iter);

    /// @brief Skip N elements.
    /// @param iter Iterator handle.
    /// @param n Number of elements to skip.
    /// @return Number of elements actually skipped.
    int64_t rt_iter_skip(void *iter, int64_t n);

#ifdef __cplusplus
}
#endif
