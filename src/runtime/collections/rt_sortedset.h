//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_sortedset.h
// Purpose: Sorted set of unique strings maintained in lexicographic order, enabling range queries
// (floor, ceil, between) and ordered forward/backward iteration.
//
// Key invariants:
//   - Elements are unique strings maintained in sorted order.
//   - All operations maintain the sorted invariant.
//   - rt_sortedset_floor returns the largest element <= key; rt_sortedset_ceil returns the smallest
//   >= key.
//   - Returned subsets from range queries are newly allocated.
//
// Ownership/Lifetime:
//   - SortedSet objects are heap-allocated; caller is responsible for lifetime management.
//   - String elements are copied into the set; caller retains ownership of input strings.
//
// Links: src/runtime/collections/rt_sortedset.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_SORTEDSET_H
#define VIPER_RT_SORTEDSET_H

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // Creation and Lifecycle
    //=============================================================================

    /// @brief Create a new empty sorted set.
    /// @return Pointer to new SortedSet object.
    void *rt_sortedset_new(void);

    /// @brief Get the number of elements in the set.
    /// @param obj SortedSet pointer.
    /// @return Number of elements.
    int64_t rt_sortedset_len(void *obj);

    /// @brief Check if the set is empty.
    /// @param obj SortedSet pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_sortedset_is_empty(void *obj);

    //=============================================================================
    // Basic Operations
    //=============================================================================

    /// @brief Add a string to the set.
    /// @param obj SortedSet pointer.
    /// @param str String to add (copied).
    /// @return 1 if string was new (added), 0 if already present.
    int8_t rt_sortedset_put(void *obj, rt_string str);

    /// @brief Remove a string from the set.
    /// @param obj SortedSet pointer.
    /// @param str String to remove.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_sortedset_drop(void *obj, rt_string str);

    /// @brief Check if a string exists in the set.
    /// @param obj SortedSet pointer.
    /// @param str String to check.
    /// @return 1 if present, 0 otherwise.
    int8_t rt_sortedset_has(void *obj, rt_string str);

    /// @brief Remove all elements from the set.
    /// @param obj SortedSet pointer.
    void rt_sortedset_clear(void *obj);

    //=============================================================================
    // Ordered Access
    //=============================================================================

    /// @brief Get the smallest element.
    /// @param obj SortedSet pointer.
    /// @return First element in sorted order, or empty string if empty.
    rt_string rt_sortedset_first(void *obj);

    /// @brief Get the largest element.
    /// @param obj SortedSet pointer.
    /// @return Last element in sorted order, or empty string if empty.
    rt_string rt_sortedset_last(void *obj);

    /// @brief Get the greatest element less than or equal to the given string.
    /// @param obj SortedSet pointer.
    /// @param str Upper bound (inclusive).
    /// @return Floor element, or empty string if none exists.
    rt_string rt_sortedset_floor(void *obj, rt_string str);

    /// @brief Get the least element greater than or equal to the given string.
    /// @param obj SortedSet pointer.
    /// @param str Lower bound (inclusive).
    /// @return Ceiling element, or empty string if none exists.
    rt_string rt_sortedset_ceil(void *obj, rt_string str);

    /// @brief Get the greatest element strictly less than the given string.
    /// @param obj SortedSet pointer.
    /// @param str Upper bound (exclusive).
    /// @return Lower element, or empty string if none exists.
    rt_string rt_sortedset_lower(void *obj, rt_string str);

    /// @brief Get the least element strictly greater than the given string.
    /// @param obj SortedSet pointer.
    /// @param str Lower bound (exclusive).
    /// @return Higher element, or empty string if none exists.
    rt_string rt_sortedset_higher(void *obj, rt_string str);

    /// @brief Get element at index in sorted order.
    /// @param obj SortedSet pointer.
    /// @param index 0-based index.
    /// @return Element at index, or empty string if out of bounds.
    rt_string rt_sortedset_at(void *obj, int64_t index);

    /// @brief Get the index of an element in sorted order.
    /// @param obj SortedSet pointer.
    /// @param str Element to find.
    /// @return Index of element, or -1 if not found.
    int64_t rt_sortedset_index_of(void *obj, rt_string str);

    //=============================================================================
    // Range Operations
    //=============================================================================

    /// @brief Get all elements in a range [from, to).
    /// @param obj SortedSet pointer.
    /// @param from Start of range (inclusive).
    /// @param to End of range (exclusive).
    /// @return Seq of elements in the range.
    void *rt_sortedset_range(void *obj, rt_string from, rt_string to);

    /// @brief Get all elements as a Seq in sorted order.
    /// @param obj SortedSet pointer.
    /// @return Seq containing all elements (sorted).
    void *rt_sortedset_items(void *obj);

    /// @brief Get the first n elements.
    /// @param obj SortedSet pointer.
    /// @param n Number of elements to get.
    /// @return Seq of first n elements.
    void *rt_sortedset_take(void *obj, int64_t n);

    /// @brief Get all elements except the first n.
    /// @param obj SortedSet pointer.
    /// @param n Number of elements to skip.
    /// @return Seq of remaining elements.
    void *rt_sortedset_skip(void *obj, int64_t n);

    //=============================================================================
    // Set Operations
    //=============================================================================

    /// @brief Create union of two sorted sets.
    /// @param obj First SortedSet pointer.
    /// @param other Second SortedSet pointer.
    /// @return New SortedSet containing elements from both.
    void *rt_sortedset_union(void *obj, void *other);

    /// @brief Create intersection of two sorted sets.
    /// @param obj First SortedSet pointer.
    /// @param other Second SortedSet pointer.
    /// @return New SortedSet containing elements in both.
    void *rt_sortedset_intersect(void *obj, void *other);

    /// @brief Create difference of two sorted sets.
    /// @param obj First SortedSet pointer.
    /// @param other Second SortedSet pointer.
    /// @return New SortedSet containing elements in obj but not in other.
    void *rt_sortedset_diff(void *obj, void *other);

    /// @brief Check if this set is a subset of another.
    /// @param obj SortedSet pointer.
    /// @param other SortedSet pointer to compare against.
    /// @return 1 if obj is a subset of other, 0 otherwise.
    int8_t rt_sortedset_is_subset(void *obj, void *other);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_SORTEDSET_H
