//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_seq.h
// Purpose: Runtime-backed dynamic sequence for Viper.Collections.Seq, providing O(1) amortized
// append, O(1) indexed access, and functional operations (map, filter, reduce).
//
// Key invariants:
//   - Indices are 0-based; out-of-bounds access traps at runtime.
//   - Append is amortized O(1) with capacity doubling.
//   - Elements are not individually reference-counted in the base Seq.
//   - Functional operations (map, filter) return new Seq objects.
//
// Ownership/Lifetime:
//   - Seq objects are heap-allocated opaque pointers.
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/collections/rt_seq.c (implementation)
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

    /// @brief Get a string element at the specified index from a string sequence.
    /// @details For seq<str> sequences (e.g. from Viper.String.Split), elements are
    ///          stored as raw rt_string pointers (not boxed). This casts directly.
    /// @param obj Opaque Seq object pointer.
    /// @param idx Index of element to retrieve.
    /// @return String element at the index; traps if out of bounds.
    struct rt_string_impl *rt_seq_get_str(void *obj, int64_t idx);

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

    //=========================================================================
    // Functional Operations - Keep, Reject, Apply, All, Any, etc.
    //=========================================================================

    /// @brief Create a new Seq containing only elements matching a predicate.
    /// @details Iterates through the Seq and includes elements for which the
    ///          predicate function returns non-zero (true).
    /// @param obj Opaque Seq object pointer. If NULL, returns empty Seq.
    /// @param pred Predicate function that takes an element and returns non-zero
    ///             to include it. If NULL, returns a clone of the original.
    /// @return New Seq containing only matching elements.
    /// @note O(n) time complexity.
    /// @note Creates a new Seq; original is not modified.
    void *rt_seq_keep(void *obj, int8_t (*pred)(void *));

    /// @brief Create a new Seq excluding elements matching a predicate.
    /// @details Inverse of Keep. Includes elements for which the predicate
    ///          returns zero (false).
    /// @param obj Opaque Seq object pointer. If NULL, returns empty Seq.
    /// @param pred Predicate function. If NULL, returns a clone of the original.
    /// @return New Seq containing only non-matching elements.
    /// @note O(n) time complexity.
    void *rt_seq_reject(void *obj, int8_t (*pred)(void *));

    /// @brief Create a new Seq by transforming each element with a function.
    /// @details Applies the transform function to each element and collects
    ///          the results into a new Seq.
    /// @param obj Opaque Seq object pointer. If NULL, returns empty Seq.
    /// @param fn Transform function that takes an element and returns a new
    ///           element. If NULL, returns a clone of the original.
    /// @return New Seq containing transformed elements.
    /// @note O(n) time complexity.
    void *rt_seq_apply(void *obj, void *(*fn)(void *));

    /// @brief Check if all elements satisfy a predicate.
    /// @details Returns true if the predicate returns non-zero for every element.
    ///          Returns true for empty sequences (vacuous truth).
    /// @param obj Opaque Seq object pointer. If NULL, returns 1 (true).
    /// @param pred Predicate function. If NULL, returns 1 (true).
    /// @return 1 if all elements match, 0 otherwise.
    /// @note O(n) worst case, but short-circuits on first non-match.
    int8_t rt_seq_all(void *obj, int8_t (*pred)(void *));

    /// @brief Check if any element satisfies a predicate.
    /// @details Returns true if the predicate returns non-zero for at least
    ///          one element. Returns false for empty sequences.
    /// @param obj Opaque Seq object pointer. If NULL, returns 0 (false).
    /// @param pred Predicate function. If NULL, returns 0 (false).
    /// @return 1 if any element matches, 0 otherwise.
    /// @note O(n) worst case, but short-circuits on first match.
    int8_t rt_seq_any(void *obj, int8_t (*pred)(void *));

    /// @brief Check if no elements satisfy a predicate.
    /// @details Returns true if the predicate returns zero for every element.
    ///          Returns true for empty sequences.
    /// @param obj Opaque Seq object pointer. If NULL, returns 1 (true).
    /// @param pred Predicate function. If NULL, returns 1 (true).
    /// @return 1 if no elements match, 0 otherwise.
    /// @note O(n) worst case, but short-circuits on first match.
    int8_t rt_seq_none(void *obj, int8_t (*pred)(void *));

    /// @brief Count elements that satisfy a predicate.
    /// @param obj Opaque Seq object pointer. If NULL, returns 0.
    /// @param pred Predicate function. If NULL, returns total length.
    /// @return Number of elements for which predicate returns non-zero.
    /// @note O(n) time complexity.
    int64_t rt_seq_count_where(void *obj, int8_t (*pred)(void *));

    /// @brief Find the first element satisfying a predicate.
    /// @param obj Opaque Seq object pointer. If NULL, returns NULL.
    /// @param pred Predicate function. If NULL, returns first element or NULL.
    /// @return First matching element, or NULL if none found.
    /// @note O(n) worst case, but short-circuits on first match.
    void *rt_seq_find_where(void *obj, int8_t (*pred)(void *));

    /// @brief Create a new Seq with the first N elements.
    /// @param obj Opaque Seq object pointer. If NULL, returns empty Seq.
    /// @param n Number of elements to take. Clamped to [0, len].
    /// @return New Seq containing at most n elements from the start.
    /// @note O(n) time complexity.
    void *rt_seq_take(void *obj, int64_t n);

    /// @brief Create a new Seq skipping the first N elements.
    /// @param obj Opaque Seq object pointer. If NULL, returns empty Seq.
    /// @param n Number of elements to skip. Clamped to [0, len].
    /// @return New Seq containing elements after the first n.
    /// @note O(n) time complexity.
    void *rt_seq_drop(void *obj, int64_t n);

    /// @brief Create a new Seq with elements taken while predicate is true.
    /// @details Takes elements from the start while the predicate returns
    ///          non-zero. Stops at the first element where predicate is false.
    /// @param obj Opaque Seq object pointer. If NULL, returns empty Seq.
    /// @param pred Predicate function. If NULL, returns clone.
    /// @return New Seq with leading elements matching predicate.
    /// @note O(n) time complexity.
    void *rt_seq_take_while(void *obj, int8_t (*pred)(void *));

    /// @brief Create a new Seq skipping elements while predicate is true.
    /// @details Skips elements from the start while the predicate returns
    ///          non-zero. Includes all elements from the first non-match.
    /// @param obj Opaque Seq object pointer. If NULL, returns empty Seq.
    /// @param pred Predicate function. If NULL, returns empty Seq.
    /// @return New Seq with elements after the leading matching ones.
    /// @note O(n) time complexity.
    void *rt_seq_drop_while(void *obj, int8_t (*pred)(void *));

    /// @brief Reduce the sequence to a single value using an accumulator.
    /// @details Applies the reducer function to each element and an accumulator,
    ///          threading the result through as the new accumulator.
    /// @param obj Opaque Seq object pointer. If NULL, returns init.
    /// @param init Initial accumulator value.
    /// @param fn Reducer function: fn(accumulator, element) -> new accumulator.
    ///           If NULL, returns init.
    /// @return Final accumulated value.
    /// @note O(n) time complexity.
    void *rt_seq_fold(void *obj, void *init, void *(*fn)(void *, void *));

#ifdef __cplusplus
}
#endif
