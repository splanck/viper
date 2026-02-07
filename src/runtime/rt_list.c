//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_list.c
// Purpose: Implement a simple object-backed list using rt_arr_obj as storage.
// Structure: [vptr | arr]
// - vptr: points to class vtable (not required for these helpers)
// - arr:  dynamic array of void* managed by rt_arr_obj_*
//
//===----------------------------------------------------------------------===//

#include "rt_list.h"

#include "rt_array_obj.h"
#include "rt_box.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/// @brief Internal List implementation structure.
///
/// The List is a dynamic collection backed by rt_arr_obj (a managed object array).
/// Unlike Seq which manages its own internal array, List delegates storage to
/// the object array system which handles reference counting automatically.
///
/// **Memory layout:**
/// ```
/// List object (GC-managed):
///   +------+-----+
///   | vptr | arr |
///   | NULL | --->|
///   +------+-|---+
///            |
///            v
/// rt_arr_obj (managed array):
///   +---+---+---+...+
///   | A | B | C |   |
///   +---+---+---+...+
/// ```
///
/// **Comparison with Seq:**
/// - List: Uses rt_arr_obj, automatic reference management
/// - Seq: Uses raw malloc'd array, more control
///
/// **Element ownership:**
/// Elements stored in the List are managed by the underlying rt_arr_obj,
/// which handles reference counting automatically.
typedef struct rt_list_impl
{
    void **vptr; ///< Vtable pointer placeholder (for OOP compatibility).
    void **arr;  ///< Pointer to the underlying object array (rt_arr_obj).
} rt_list_impl;

/// @brief Finalizer callback invoked when a List is garbage collected.
///
/// This function is automatically called by Viper's garbage collector when a
/// List object becomes unreachable. It releases the underlying object array,
/// which will in turn release references to all contained elements.
///
/// @param obj Pointer to the List object being finalized. May be NULL (no-op).
///
/// @note The underlying rt_arr_obj handles element reference counting.
/// @note This function is idempotent - safe to call on already-finalized lists.
///
/// @see rt_list_clear For removing elements without finalization
static void rt_list_finalize(void *obj)
{
    if (!obj)
        return;
    rt_list_impl *L = (rt_list_impl *)obj;
    if (L->arr)
    {
        rt_arr_obj_release(L->arr);
        L->arr = NULL;
    }
}

/// @brief Creates a new empty List.
///
/// Allocates and initializes a List data structure for storing a dynamic
/// collection of objects. The List starts empty with no underlying array
/// allocated until elements are added.
///
/// **Lazy allocation:**
/// The internal array is not allocated until the first element is added.
/// This makes creating empty Lists very lightweight.
///
/// **Usage example:**
/// ```
/// Dim list = List.New()
/// list.Add("first")
/// list.Add("second")
/// list.Add("third")
/// Print list.Count   ' Outputs: 3
/// Print list.Item(0) ' Outputs: first
/// ```
///
/// @return A pointer to the newly created List object, or NULL if memory
///         allocation fails.
///
/// @note The List uses rt_arr_obj for storage with automatic reference management.
/// @note Thread safety: Not thread-safe. External synchronization required.
///
/// @see rt_list_push For adding elements
/// @see rt_list_get For accessing elements
/// @see rt_list_finalize For cleanup behavior
void *rt_ns_list_new(void)
{
    // Allocate object payload with header via object allocator to match object lifetime rules
    rt_list_impl *list = (rt_list_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_list_impl));
    if (!list)
        return NULL;
    list->vptr = NULL;
    list->arr = NULL;
    rt_obj_set_finalizer(list, rt_list_finalize);
    return list;
}

/// @brief Helper to cast a void pointer to a List implementation pointer.
/// @param p Raw pointer to cast.
/// @return Pointer cast to rt_list_impl*.
static inline rt_list_impl *as_list(void *p)
{
    return (rt_list_impl *)p;
}

/// @brief Returns the number of elements in the List.
///
/// This function returns how many elements are currently stored in the List.
/// The count is maintained by the underlying array and returned in O(1) time.
///
/// @param list Pointer to a List object. If NULL, returns 0.
///
/// @return The number of elements in the List (>= 0). Returns 0 if list is NULL.
///
/// @note O(1) time complexity.
///
/// @see rt_list_push For operations that increase the count
/// @see rt_list_remove_at For operations that decrease the count
int64_t rt_list_len(void *list)
{
    if (!list)
        return 0;
    rt_list_impl *L = as_list(list);
    return (int64_t)rt_arr_obj_len(L->arr);
}

/// @brief Removes all elements from the List.
///
/// Clears the List by releasing the underlying array. This also releases
/// references to all contained elements, which may cause them to be freed
/// if no other references exist.
///
/// **After clear:**
/// - Count becomes 0
/// - All element references are released
/// - The List can be reused for new elements
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("b")
/// Print list.Count  ' Outputs: 2
/// list.Clear()
/// Print list.Count  ' Outputs: 0
/// ```
///
/// @param list Pointer to a List object. If NULL, this is a no-op.
///
/// @note The underlying rt_arr_obj and element references are released.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_finalize For complete cleanup during garbage collection
void rt_list_clear(void *list)
{
    if (!list)
        return;
    rt_list_impl *L = as_list(list);
    if (L->arr)
    {
        rt_arr_obj_release(L->arr);
        L->arr = NULL;
    }
}

/// @brief Adds an element to the end of the List.
///
/// Appends a new element after the current last element. The underlying array
/// automatically grows to accommodate the new element.
///
/// **Visual example:**
/// ```
/// Before Add(D):  [A, B, C]      count=3
/// After Add(D):   [A, B, C, D]   count=4
/// ```
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("first")
/// list.Add("second")
/// list.Add("third")
/// Print list.Count  ' Outputs: 3
/// ```
///
/// @param list Pointer to a List object. If NULL, this is a no-op.
/// @param elem The element to add. Reference is retained by the List.
///
/// @note O(1) amortized time complexity. Occasional O(n) when resizing.
/// @note The List retains a reference to elem via rt_arr_obj.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_insert For inserting at arbitrary positions
/// @see rt_list_remove_at For removing elements
void rt_list_push(void *list, void *elem)
{
    if (!list)
        return;
    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    void **arr2 = rt_arr_obj_resize(L->arr, len + 1);
    if (!arr2)
        return;
    L->arr = arr2;
    rt_arr_obj_put(L->arr, len, elem);
}

/// @brief Returns the element at the specified index.
///
/// Provides O(1) random access to any element in the List. Indices are
/// zero-based, so valid indices range from 0 to count-1.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("b")
/// list.Add("c")
/// Print list.Item(0)  ' Outputs: a
/// Print list.Item(1)  ' Outputs: b
/// Print list.Item(2)  ' Outputs: c
/// ```
///
/// @param list Pointer to a List object. Must not be NULL.
/// @param index Zero-based index of the element to retrieve (0 to count-1).
///
/// @return The element at the specified index.
///
/// @note O(1) time complexity.
/// @note Traps with "rt_list_get: null list" if list is NULL.
/// @note Traps with "rt_list_get: negative index" if index < 0.
/// @note Traps with "rt_list_get: index out of bounds" if index >= count.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_set For modifying an element
/// @see rt_list_len For getting the valid index range
void *rt_list_get(void *list, int64_t index)
{
    if (!list)
        rt_trap("rt_list_get: null list");
    if (index < 0)
        rt_trap("rt_list_get: negative index");
    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if ((uint64_t)index >= (uint64_t)len)
        rt_trap("rt_list_get: index out of bounds");
    return rt_arr_obj_get(L->arr, (size_t)index);
}

/// @brief Replaces the element at the specified index.
///
/// Sets a new value at the given index. The previous element's reference is
/// released, and a reference to the new element is retained. The index must
/// refer to an existing element - this function cannot extend the List.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("b")
/// list.SetItem(0, "x")
/// Print list.Item(0)  ' Outputs: x
/// Print list.Item(1)  ' Outputs: b
/// ```
///
/// @param list Pointer to a List object. Must not be NULL.
/// @param index Zero-based index of the element to modify (0 to count-1).
/// @param elem The new element to store at this index.
///
/// @note O(1) time complexity.
/// @note The old element's reference is released, the new one is retained.
/// @note Traps with "rt_list_set: null list" if list is NULL.
/// @note Traps with "rt_list_set: negative index" if index < 0.
/// @note Traps with "rt_list_set: index out of bounds" if index >= count.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_get For reading an element
/// @see rt_list_push For adding new elements
void rt_list_set(void *list, int64_t index, void *elem)
{
    if (!list)
        rt_trap("rt_list_set: null list");
    if (index < 0)
        rt_trap("rt_list_set: negative index");
    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if ((uint64_t)index >= (uint64_t)len)
        rt_trap("rt_list_set: index out of bounds");
    rt_arr_obj_put(L->arr, (size_t)index, elem);
}

/// @brief Removes the element at the specified index.
///
/// Removes the element at the given index and shifts all subsequent elements
/// one position to the left to fill the gap. The removed element's reference
/// is released.
///
/// **Visual example:**
/// ```
/// Before RemoveAt(1):  [A, B, C, D]   count=4
/// After RemoveAt(1):   [A, C, D]      count=3
/// ```
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("b")
/// list.Add("c")
/// list.RemoveAt(1)
/// ' list is now: [a, c]
/// Print list.Count  ' Outputs: 2
/// ```
///
/// @param list Pointer to a List object. Must not be NULL.
/// @param index Zero-based index of the element to remove (0 to count-1).
///
/// @note O(n) time complexity due to element shifting.
/// @note The removed element's reference is released.
/// @note Traps with "rt_list_remove_at: null list" if list is NULL.
/// @note Traps with "rt_list_remove_at: negative index" if index < 0.
/// @note Traps with "rt_list_remove_at: index out of bounds" if index >= count.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_remove For removing by element value
/// @see rt_list_push For adding elements
void rt_list_remove_at(void *list, int64_t index)
{
    if (!list)
        rt_trap("rt_list_remove_at: null list");
    if (index < 0)
        rt_trap("rt_list_remove_at: negative index");
    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if ((uint64_t)index >= (uint64_t)len)
        rt_trap("rt_list_remove_at: index out of bounds");
    // Shift elements left from index
    if (len > 0)
    {
        // Release element at index by overwriting it via put with next (or NULL for last)
        for (size_t i = (size_t)index; i + 1 < len; ++i)
        {
            void *next = L->arr[i + 1];
            rt_arr_obj_put(L->arr, i, next);
        }
        // Clear last slot
        rt_arr_obj_put(L->arr, len - 1, NULL);
        // Shrink storage
        L->arr = rt_arr_obj_resize(L->arr, len - 1);
    }
}

/// @brief Finds the first occurrence of an element in the List.
///
/// Searches for an element by pointer equality (identity comparison, not
/// value equality). Returns the index of the first match, or -1 if not found.
///
/// **Comparison semantics:**
/// This function compares pointers, not values. Two objects with the same
/// content but different memory addresses will NOT match.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// Dim obj1 = SomeObject.New()
/// Dim obj2 = SomeObject.New()
/// list.Add(obj1)
/// list.Add(obj2)
/// Print list.Find(obj1)   ' Outputs: 0
/// Print list.Find(obj2)   ' Outputs: 1
/// ```
///
/// @param list Pointer to a List object. If NULL, returns -1.
/// @param elem The element to search for (compared by content for boxed values).
///
/// @return The zero-based index of the first occurrence, or -1 if not found
///         or list is NULL.
///
/// @note O(n) time complexity - linear search from the beginning.
/// @note Boxed values are compared by content; non-boxed by pointer identity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_has For boolean membership check
int64_t rt_list_find(void *list, void *elem)
{
    if (!list)
        return -1;

    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);

    for (size_t i = 0; i < len; ++i)
    {
        if (rt_box_equal(L->arr[i], elem))
            return (int64_t)i;
    }

    return -1;
}

/// @brief Checks whether the List contains a specific element.
///
/// Tests if the element is present in the List using content-aware equality.
/// This is a convenience wrapper around rt_list_find that returns a boolean.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// Dim obj = SomeObject.New()
/// list.Add(obj)
/// Print list.Has(obj)     ' Outputs: True
/// Print list.Has(Nothing) ' Outputs: False
/// ```
///
/// @param list Pointer to a List object. If NULL, returns 0 (false).
/// @param elem The element to search for (compared by content for boxed values).
///
/// @return 1 (true) if the element is found, 0 (false) otherwise.
///
/// @note O(n) time complexity - linear search.
/// @note Boxed values are compared by content; non-boxed by pointer identity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_find For getting the index of the element
int8_t rt_list_has(void *list, void *elem)
{
    return rt_list_find(list, elem) >= 0 ? 1 : 0;
}

/// @brief Inserts an element at the specified position.
///
/// Inserts a new element at the given index, shifting all subsequent elements
/// one position to the right. Unlike SetItem, Insert grows the List by one.
///
/// **Visual example:**
/// ```
/// Before Insert(1, X):  [A, B, C]      count=3
/// After Insert(1, X):   [A, X, B, C]   count=4
/// ```
///
/// **Valid indices:**
/// - 0: Insert at the beginning (before all elements)
/// - count: Insert at the end (equivalent to Add)
/// - Any value from 0 to count (inclusive)
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("c")
/// list.Insert(1, "b")    ' Insert between a and c
/// ' list is now: [a, b, c]
/// ```
///
/// @param list Pointer to a List object. Must not be NULL.
/// @param index Position to insert at (0 to count inclusive).
/// @param elem The element to insert. Reference is retained by the List.
///
/// @note O(n) time complexity due to element shifting.
/// @note The List retains a reference to elem via rt_arr_obj.
/// @note Traps with "List.Insert: null list" if list is NULL.
/// @note Traps with "List.Insert: negative index" if index < 0.
/// @note Traps with "List.Insert: index out of bounds" if index > count.
/// @note Traps with "List.Insert: memory allocation failed" on allocation failure.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_push For appending to the end (O(1))
/// @see rt_list_remove_at For removing at an index
void rt_list_insert(void *list, int64_t index, void *elem)
{
    if (!list)
        rt_trap("List.Insert: null list");
    if (index < 0)
        rt_trap("List.Insert: negative index");

    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if ((uint64_t)index > (uint64_t)len)
        rt_trap("List.Insert: index out of bounds");

    void **arr2 = rt_arr_obj_resize(L->arr, len + 1);
    if (!arr2)
        rt_trap("List.Insert: memory allocation failed");
    L->arr = arr2;

    // Shift elements right from the end to index.
    for (size_t i = len; i > (size_t)index; --i)
    {
        void *prev = L->arr[i - 1];
        rt_arr_obj_put(L->arr, i, prev);
    }

    rt_arr_obj_put(L->arr, (size_t)index, elem);
}

/// @brief Removes the first occurrence of an element from the List.
///
/// Searches for the element by content-aware equality and removes the first match.
/// If the element is not found, the List remains unchanged and false is returned.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// Dim obj = SomeObject.New()
/// list.Add(obj)
/// list.Add("other")
/// Print list.Remove(obj)   ' Outputs: True (removed)
/// Print list.Remove(obj)   ' Outputs: False (already removed)
/// ```
///
/// @param list Pointer to a List object. If NULL, returns 0 (false).
/// @param elem The element to remove (compared by content for boxed values).
///
/// @return 1 (true) if the element was found and removed, 0 (false) otherwise.
///
/// @note O(n) time complexity for search + O(n) for removal = O(n) total.
/// @note Only removes the first occurrence if duplicates exist.
/// @note The removed element's reference is released.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_list_remove_at For removing by index
/// @see rt_list_find For finding without removing
int8_t rt_list_remove(void *list, void *elem)
{
    int64_t idx = rt_list_find(list, elem);
    if (idx < 0)
        return 0;
    rt_list_remove_at(list, idx);
    return 1;
}

/// @brief Creates a new List containing a slice of elements.
///
/// Returns a new List containing elements from the specified range.
/// The original List is not modified.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("b")
/// list.Add("c")
/// list.Add("d")
/// Dim slice = list.Slice(1, 3)
/// ' slice contains ["b", "c"]
/// ```
///
/// @param list  Pointer to a List object.
/// @param start Start index (inclusive, clamped to 0).
/// @param end   End index (exclusive, clamped to Count).
///
/// @return New List containing the elements in the range.
///
/// @note O(k) time where k = end - start.
/// @note Thread safety: Not thread-safe.
void *rt_list_slice(void *list, int64_t start, int64_t end)
{
    void *result = rt_ns_list_new();
    if (!result)
        return NULL;

    if (!list)
        return result;

    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);

    // Clamp indices
    if (start < 0)
        start = 0;
    if (end < 0)
        end = 0;
    if ((size_t)start > len)
        start = (int64_t)len;
    if ((size_t)end > len)
        end = (int64_t)len;
    if (start >= end)
        return result;

    // Copy elements
    for (int64_t i = start; i < end; i++)
    {
        void *elem = rt_arr_obj_get(L->arr, (size_t)i);
        rt_list_push(result, elem);
    }

    return result;
}

/// @brief Reverses the order of elements in the List in place.
///
/// Swaps elements from both ends toward the center, modifying
/// the original List.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("b")
/// list.Add("c")
/// list.Flip()
/// ' list now contains ["c", "b", "a"]
/// ```
///
/// @param list Pointer to a List object. If NULL, this is a no-op.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
void rt_list_flip(void *list)
{
    if (!list)
        return;

    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if (len < 2)
        return;

    // Swap elements from both ends toward center
    for (size_t i = 0; i < len / 2; i++)
    {
        size_t j = len - 1 - i;
        void *a = L->arr[i];
        void *b = L->arr[j];
        // Direct swap without reference counting (elements stay in list)
        L->arr[i] = b;
        L->arr[j] = a;
    }
}

/// @brief Returns the first element in the List.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("b")
/// Print list.First()  ' Outputs: a
/// ```
///
/// @param list Pointer to a List object.
///
/// @return The first element, or NULL if the List is empty or NULL.
///
/// @note O(1) time complexity.
/// @note Thread safety: Not thread-safe.
void *rt_list_first(void *list)
{
    if (!list)
        return NULL;

    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if (len == 0)
        return NULL;

    return rt_arr_obj_get(L->arr, 0);
}

/// @brief Returns the last element in the List.
///
/// **Example:**
/// ```
/// Dim list = List.New()
/// list.Add("a")
/// list.Add("b")
/// Print list.Last()  ' Outputs: b
/// ```
///
/// @param list Pointer to a List object.
///
/// @return The last element, or NULL if the List is empty or NULL.
///
/// @note O(1) time complexity.
/// @note Thread safety: Not thread-safe.
void *rt_list_last(void *list)
{
    if (!list)
        return NULL;

    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if (len == 0)
        return NULL;

    return rt_arr_obj_get(L->arr, len - 1);
}
