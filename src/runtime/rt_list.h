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

#ifdef __cplusplus
}
#endif
