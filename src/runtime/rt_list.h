//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_list.h
// Purpose: Runtime-backed list of object references for Viper.Collections.List.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// What: Allocate a new List instance (opaque object pointer with vptr at offset 0).
    /// Why:  Provide the concrete runtime backing for Viper.Collections.List.
    /// How:  Allocates internal storage (initial capacity may be > 0) and initializes
    ///       bookkeeping fields. Returns a heap-managed object; reference counting applies.
    ///
    /// @return Opaque pointer to the new List object; NULL on allocation failure.
    /// @thread-safety Not thread-safe; caller is responsible for synchronization.
    void *rt_ns_list_new(void);

    /// What: Get the number of elements in the list.
    /// Why:  Expose List.Count property to the runtime.
    /// How:  Returns the current logical length stored by the list.
    ///
    /// @param list Opaque List object pointer.
    /// @return Number of elements currently in the list.
    int64_t rt_list_get_count(void *list);

    /// What: Append @p elem to the end of the list.
    /// Why:  Support dynamic growth for collection operations.
    /// How:  Retains @p elem (if non-null), grows internal storage geometrically if needed,
    ///       and writes the element at the end.
    ///
    /// @param list Opaque List object pointer.
    /// @param elem Opaque object element pointer (may be NULL to represent empty slot).
    /// @post Count increases by 1 on success.
    void rt_list_add(void *list, void *elem);

    /// What: Remove all elements from the list.
    /// Why:  Provide a fast way to reuse list storage without reallocation.
    /// How:  Releases all retained elements and resets the length to zero; capacity may be kept.
    ///
    /// @param list Opaque List object pointer.
    /// @post Count becomes zero; capacity is implementation-defined.
    void rt_list_clear(void *list);

    /// What: Remove the element at @p index.
    /// Why:  Support positional removal semantics.
    /// How:  Releases the element and compacts the tail by shifting left.
    ///
    /// @param list  Opaque List object pointer.
    /// @param index 0-based index; must satisfy 0 <= index < Count.
    /// @pre Index must be within bounds; violating may trap at runtime.
    void rt_list_remove_at(void *list, int64_t index);

    /// What: Get the element at @p index (retained).
    /// Why:  Allow safe use of the returned element beyond subsequent mutations.
    /// How:  Increments the element's refcount before returning.
    ///
    /// @param list  Opaque List object pointer.
    /// @param index 0-based index; must satisfy 0 <= index < Count.
    /// @return The element pointer (may be NULL); caller must release.
    void *rt_list_get_item(void *list, int64_t index);

    /// What: Set the element at @p index to @p elem.
    /// Why:  Provide indexed update with correct reference management.
    /// How:  Retains @p elem (if non-null) and releases the previously stored value.
    ///
    /// @param list  Opaque List object pointer.
    /// @param index 0-based index; must satisfy 0 <= index < Count.
    /// @param elem  Replacement element pointer (may be NULL).
    void rt_list_set_item(void *list, int64_t index, void *elem);

    /// What: Check whether the list contains @p elem.
    /// Why:  Provide a common membership query for Viper.Collections.List.
    /// How:  Scans the list and compares elements using the same notion as Seq.Has/Seq.Find
    ///       (reference equality).
    ///
    /// @param list Opaque List object pointer.
    /// @param elem Element to look for (may be NULL).
    /// @return 1 if present, 0 otherwise.
    int8_t rt_list_has(void *list, void *elem);

    /// What: Find the first index of @p elem in the list.
    /// Why:  Enable search and removal patterns without manual iteration.
    /// How:  Scans the list left-to-right and compares by reference equality.
    ///
    /// @param list Opaque List object pointer.
    /// @param elem Element to look for (may be NULL).
    /// @return Index of the first matching element, or -1 when not found.
    int64_t rt_list_find(void *list, void *elem);

    /// What: Insert @p elem at @p index, shifting elements to the right.
    /// Why:  Support positional insertion for dynamic list operations.
    /// How:  Grows the backing storage by one and shifts elements right; retains inserted value.
    ///
    /// @param list  Opaque List object pointer.
    /// @param index 0-based insert position; must satisfy 0 <= index <= Count.
    /// @param elem  Element to insert (may be NULL).
    /// @pre Index must be within bounds; violating traps at runtime.
    void rt_list_insert(void *list, int64_t index, void *elem);

    /// What: Remove the first occurrence of @p elem from the list.
    /// Why:  Provide a common removal helper with boolean success reporting.
    /// How:  Searches for @p elem using reference equality and removes it when found.
    ///
    /// @param list Opaque List object pointer.
    /// @param elem Element to remove (may be NULL).
    /// @return 1 when an element was removed, 0 otherwise.
    int8_t rt_list_remove(void *list, void *elem);

    /// What: Create a new List containing elements from @p start to @p end.
    /// Why:  Support sub-list extraction for common list operations.
    /// How:  Creates a new List and copies elements in the specified range.
    ///
    /// @param list  Opaque List object pointer.
    /// @param start 0-based start index (inclusive, clamped to 0).
    /// @param end   0-based end index (exclusive, clamped to Count).
    /// @return New List containing the slice; empty List if range is invalid.
    void *rt_list_slice(void *list, int64_t start, int64_t end);

    /// What: Reverse the order of elements in the list in place.
    /// Why:  Support common list transformation without creating a new list.
    /// How:  Swaps elements from both ends toward the center.
    ///
    /// @param list Opaque List object pointer.
    void rt_list_flip(void *list);

    /// What: Get the first element in the list.
    /// Why:  Convenience method for common head/first access pattern.
    /// How:  Returns element at index 0.
    ///
    /// @param list Opaque List object pointer.
    /// @return The first element, or NULL if list is empty.
    void *rt_list_first(void *list);

    /// What: Get the last element in the list.
    /// Why:  Convenience method for common tail/last access pattern.
    /// How:  Returns element at index Count-1.
    ///
    /// @param list Opaque List object pointer.
    /// @return The last element, or NULL if list is empty.
    void *rt_list_last(void *list);

#ifdef __cplusplus
}
#endif
