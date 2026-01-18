//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_seq.h
// Purpose: Runtime-backed dynamic sequence for Viper.Collections.Seq.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty sequence with default capacity.
    /// @return Opaque pointer to the new Seq object.
    void *rt_seq_new(void);

    /// @brief Create a new empty sequence with specified initial capacity.
    /// @param cap Initial capacity (minimum 1).
    /// @return Opaque pointer to the new Seq object.
    void *rt_seq_with_capacity(int64_t cap);

    /// @brief Get the number of elements in the sequence.
    /// @param obj Opaque Seq object pointer.
    /// @return Number of elements currently in the sequence.
    int64_t rt_seq_len(void *obj);

    /// @brief Get the current capacity of the sequence.
    /// @param obj Opaque Seq object pointer.
    /// @return Current capacity.
    int64_t rt_seq_cap(void *obj);

    /// @brief Check if the sequence is empty.
    /// @param obj Opaque Seq object pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_seq_is_empty(void *obj);

    /// @brief Get the element at the specified index.
    /// @param obj Opaque Seq object pointer.
    /// @param idx Index of element to retrieve.
    /// @return Element at the index; traps if out of bounds.
    void *rt_seq_get(void *obj, int64_t idx);

    /// @brief Set the element at the specified index.
    /// @param obj Opaque Seq object pointer.
    /// @param idx Index of element to set.
    /// @param val Value to store.
    void rt_seq_set(void *obj, int64_t idx, void *val);

    /// @brief Add an element to the end of the sequence.
    /// @param obj Opaque Seq object pointer.
    /// @param val Element to add.
    void rt_seq_push(void *obj, void *val);

    /// @brief Append all elements of @p other onto @p obj.
    /// @details Preserves element ordering. Self-appends are supported: when @p obj == @p other,
    ///          the operation doubles the original sequence contents without looping indefinitely.
    /// @param obj Opaque Seq object pointer.
    /// @param other Opaque Seq object pointer whose elements will be appended (treated as empty
    /// when NULL).
    void rt_seq_push_all(void *obj, void *other);

    /// @brief Remove and return the last element from the sequence.
    /// @param obj Opaque Seq object pointer.
    /// @return The removed element; traps if empty.
    void *rt_seq_pop(void *obj);

    /// @brief Get the last element without removing it.
    /// @param obj Opaque Seq object pointer.
    /// @return The last element; traps if empty.
    void *rt_seq_peek(void *obj);

    /// @brief Get the first element.
    /// @param obj Opaque Seq object pointer.
    /// @return The first element; traps if empty.
    void *rt_seq_first(void *obj);

    /// @brief Get the last element.
    /// @param obj Opaque Seq object pointer.
    /// @return The last element; traps if empty.
    void *rt_seq_last(void *obj);

    /// @brief Insert an element at the specified position.
    /// @param obj Opaque Seq object pointer.
    /// @param idx Position to insert at (0 to len inclusive).
    /// @param val Element to insert.
    void rt_seq_insert(void *obj, int64_t idx, void *val);

    /// @brief Remove and return the element at the specified position.
    /// @param obj Opaque Seq object pointer.
    /// @param idx Position to remove from.
    /// @return The removed element; traps if out of bounds.
    void *rt_seq_remove(void *obj, int64_t idx);

    /// @brief Remove all elements from the sequence.
    /// @param obj Opaque Seq object pointer.
    void rt_seq_clear(void *obj);

    /// @brief Find the index of an element in the sequence.
    /// @param obj Opaque Seq object pointer.
    /// @param val Element to find (compared by pointer equality).
    /// @return Index of element, or -1 if not found.
    int64_t rt_seq_find(void *obj, void *val);

    /// @brief Check if the sequence contains an element.
    /// @param obj Opaque Seq object pointer.
    /// @param val Element to check for (compared by pointer equality).
    /// @return 1 if found, 0 otherwise.
    int8_t rt_seq_has(void *obj, void *val);

    /// @brief Reverse the elements in the sequence in place.
    /// @param obj Opaque Seq object pointer.
    void rt_seq_reverse(void *obj);

    /// @brief Shuffle the elements in the sequence in place.
    /// @details Uses an in-place Fisher–Yates shuffle driven by the same deterministic RNG as
    ///          Viper.Random.NextInt (so Viper.Random.Seed influences the result).
    /// @param obj Opaque Seq object pointer.
    void rt_seq_shuffle(void *obj);

    /// @brief Create a new sequence containing elements from [start, end).
    /// @param obj Source Seq object pointer.
    /// @param start Start index (inclusive, clamped to 0).
    /// @param end End index (exclusive, clamped to len).
    /// @return New sequence containing the slice.
    void *rt_seq_slice(void *obj, int64_t start, int64_t end);

    /// @brief Create a shallow copy of the sequence.
    /// @param obj Source Seq object pointer.
    /// @return New sequence with same elements.
    void *rt_seq_clone(void *obj);

    /// @brief Sort the elements in the sequence in ascending order.
    /// @details Uses a stable merge sort algorithm. Elements are compared
    ///          using string comparison if they are strings, or by pointer
    ///          value for other objects. For custom ordering, use SortBy.
    /// @param obj Opaque Seq object pointer. If NULL, this is a no-op.
    /// @note O(n log n) time complexity.
    /// @note Modifies the Seq in place (no new allocation).
    /// @note Thread safety: Not thread-safe.
    /// @see rt_seq_sort_by For custom comparison functions
    void rt_seq_sort(void *obj);

    /// @brief Sort the elements using a custom comparison function.
    /// @details Uses a stable merge sort. The comparison function should
    ///          return negative if a < b, zero if equal, positive if a > b.
    /// @param obj Opaque Seq object pointer. If NULL, this is a no-op.
    /// @param cmp Comparison function receiving two element pointers.
    /// @note O(n log n) time complexity.
    /// @note Modifies the Seq in place (no new allocation).
    /// @note Thread safety: Not thread-safe.
    void rt_seq_sort_by(void *obj, int64_t (*cmp)(void *, void *));

    /// @brief Sort the elements in descending order.
    /// @details Uses a stable merge sort algorithm. Elements are compared
    ///          using string comparison if they are strings, or by pointer
    ///          value for other objects. Results are reversed from rt_seq_sort.
    /// @param obj Opaque Seq object pointer. If NULL, this is a no-op.
    /// @note O(n log n) time complexity.
    /// @note Modifies the Seq in place (no new allocation).
    /// @note Thread safety: Not thread-safe.
    void rt_seq_sort_desc(void *obj);

#ifdef __cplusplus
}
#endif
