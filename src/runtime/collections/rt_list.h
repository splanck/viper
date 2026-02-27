//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_list.h
// Purpose: Runtime-backed dynamic list of object references for Viper.Collections.List, providing
// append, insert, remove, sort, and indexed access with automatic growth.
//
// Key invariants:
//   - Elements are reference-managed: retained on store, released on overwrite or removal.
//   - Indices are 0-based; out-of-bounds access traps at runtime.
//   - rt_ns_list_new returns a new empty list with refcount 1.
//   - Sort operations use a stable algorithm preserving relative order of equal elements.
//
// Ownership/Lifetime:
//   - List owns references to its elements and releases them on removal or destruction.
//   - List lifetime is managed via reference counting; use retain/release to share.
//   - Callers own the initial reference returned by rt_ns_list_new.
//
// Links: src/runtime/collections/rt_list.c (implementation), src/runtime/core/rt_heap.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Allocate a new List instance (opaque object pointer with vptr at offset 0).
    /// @details Provides the concrete runtime backing for Viper.Collections.List.
    ///          Allocates internal storage (initial capacity may be > 0) and initializes
    ///          bookkeeping fields. The returned object is heap-managed and subject to
    ///          reference counting.
    /// @return Opaque pointer to the new List object; NULL on allocation failure.
    /// @thread-safety Not thread-safe; caller is responsible for synchronization.
    void *rt_ns_list_new(void);

    /// @brief Get the number of elements in the list.
    /// @details Exposes the List.Len property to the runtime by returning the
    ///          current logical length stored by the list.
    /// @param list Opaque List object pointer.
    /// @return Number of elements currently in the list.
    int64_t rt_list_len(void *list);

    /// @brief Append an element to the end of the list.
    /// @details Retains the element (if non-null), grows internal storage
    ///          geometrically if needed, and writes the element at the end.
    ///          Supports dynamic growth for collection operations.
    /// @param list Opaque List object pointer.
    /// @param elem Opaque object element pointer (may be NULL to represent empty slot).
    /// @post Count increases by 1 on success.
    void rt_list_push(void *list, void *elem);

    /// @brief Remove all elements from the list.
    /// @details Releases all retained elements and resets the length to zero.
    ///          Provides a fast way to reuse list storage without reallocation;
    ///          capacity may be retained.
    /// @param list Opaque List object pointer.
    /// @post Count becomes zero; capacity is implementation-defined.
    void rt_list_clear(void *list);

    /// @brief Remove the element at a specific index.
    /// @details Releases the element at the given position and compacts the
    ///          remaining tail by shifting elements left. Supports positional
    ///          removal semantics.
    /// @param list  Opaque List object pointer.
    /// @param index 0-based index; must satisfy 0 <= index < Count.
    /// @pre Index must be within bounds; violating may trap at runtime.
    void rt_list_remove_at(void *list, int64_t index);

    /// @brief Get the element at a specific index (retained).
    /// @details Increments the element's refcount before returning to allow
    ///          safe use of the returned element beyond subsequent list mutations.
    /// @param list  Opaque List object pointer.
    /// @param index 0-based index; must satisfy 0 <= index < Count.
    /// @return The element pointer (may be NULL); caller must release.
    void *rt_list_get(void *list, int64_t index);

    /// @brief Set the element at a specific index to a new value.
    /// @details Retains the new element (if non-null) and releases the previously
    ///          stored value. Provides indexed update with correct reference management.
    /// @param list  Opaque List object pointer.
    /// @param index 0-based index; must satisfy 0 <= index < Count.
    /// @param elem  Replacement element pointer (may be NULL).
    void rt_list_set(void *list, int64_t index, void *elem);

    /// @brief Check whether the list contains a specific element.
    /// @details Scans the list and compares elements using reference equality
    ///          (same semantics as Seq.Has/Seq.Find). Provides a common membership
    ///          query for Viper.Collections.List.
    /// @param list Opaque List object pointer.
    /// @param elem Element to look for (may be NULL).
    /// @return 1 if present, 0 otherwise.
    int8_t rt_list_has(void *list, void *elem);

    /// @brief Find the first index of an element in the list.
    /// @details Scans the list left-to-right and compares by reference equality.
    ///          Enables search and removal patterns without manual iteration.
    /// @param list Opaque List object pointer.
    /// @param elem Element to look for (may be NULL).
    /// @return Index of the first matching element, or -1 when not found.
    int64_t rt_list_find(void *list, void *elem);

    /// @brief Insert an element at a specific index, shifting elements right.
    /// @details Grows the backing storage by one and shifts elements at and after
    ///          the index to the right. Retains the inserted value. Supports
    ///          positional insertion for dynamic list operations.
    /// @param list  Opaque List object pointer.
    /// @param index 0-based insert position; must satisfy 0 <= index <= Count.
    /// @param elem  Element to insert (may be NULL).
    /// @pre Index must be within bounds; violating traps at runtime.
    void rt_list_insert(void *list, int64_t index, void *elem);

    /// @brief Remove the first occurrence of an element from the list.
    /// @details Searches for the element using reference equality and removes it
    ///          when found. Provides a common removal helper with boolean success
    ///          reporting.
    /// @param list Opaque List object pointer.
    /// @param elem Element to remove (may be NULL).
    /// @return 1 when an element was removed, 0 otherwise.
    int8_t rt_list_remove(void *list, void *elem);

    /// @brief Create a new List containing elements from a range.
    /// @details Creates a new List and copies elements in the specified range.
    ///          Supports sub-list extraction for common list operations.
    /// @param list  Opaque List object pointer.
    /// @param start 0-based start index (inclusive, clamped to 0).
    /// @param end   0-based end index (exclusive, clamped to Count).
    /// @return New List containing the slice; empty List if range is invalid.
    void *rt_list_slice(void *list, int64_t start, int64_t end);

    /// @brief Reverse the order of elements in the list in place.
    /// @details Swaps elements from both ends toward the center. Supports common
    ///          list transformation without creating a new list.
    /// @param list Opaque List object pointer.
    void rt_list_reverse(void *list);

    /// @brief Get the first element in the list.
    /// @details Convenience method for the common head/first access pattern.
    ///          Returns the element at index 0.
    /// @param list Opaque List object pointer.
    /// @return The first element, or NULL if the list is empty.
    void *rt_list_first(void *list);

    /// @brief Get the last element in the list.
    /// @details Convenience method for the common tail/last access pattern.
    ///          Returns the element at index Count-1.
    /// @param list Opaque List object pointer.
    /// @return The last element, or NULL if the list is empty.
    void *rt_list_last(void *list);

    /// @brief Check whether the list is empty.
    /// @param list Opaque List object pointer.
    /// @return 1 if empty (or NULL), 0 otherwise.
    int8_t rt_list_is_empty(void *list);

    /// @brief Remove and return the last element from the list.
    /// @param list Opaque List object pointer. Must not be NULL.
    /// @return The removed element, or traps if the list is empty.
    void *rt_list_pop(void *list);

    /// @brief Sort the list in ascending order (default comparison).
    /// @param list Opaque List object pointer.
    void rt_list_sort(void *list);

    /// @brief Sort the list in descending order.
    /// @param list Opaque List object pointer.
    void rt_list_sort_desc(void *list);

#ifdef __cplusplus
}
#endif
