//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_seq.c
// Purpose: Implements Viper.Collections.Seq, the primary dynamic growable array
//   for the Viper runtime. Seq is the most general and widely-used collection:
//   it stores heterogeneous object references, supports O(1) amortized append,
//   O(1) random access, O(n) insert/remove, and a rich functional API (map,
//   filter, reduce, sort, reverse, zip).
//
// Key invariants:
//   - Initial capacity is SEQ_DEFAULT_CAP (16); grows by SEQ_GROWTH_FACTOR (2).
//   - The items array is a separate malloc allocation; the header is GC-managed.
//   - len is the number of valid elements; cap is the allocated array size.
//     Accessing index >= len is always an error (returns NULL or traps).
//   - Seq does NOT retain elements (no rt_obj_retain on append); it stores raw
//     void* pointers. Element lifetime is the caller's responsibility.
//   - Sorting uses qsort with a comparator appropriate to element type
//     (string lexicographic order is the default).
//   - rt_seq_get returns NULL for out-of-bounds indices (no trap for read).
//   - Not thread-safe; external synchronization required for concurrent writes.
//
// Ownership/Lifetime:
//   - Seq objects are GC-managed (rt_obj_new_i64). The items array is
//     malloc-managed and freed by the GC finalizer (seq_finalizer).
//
// Links: src/runtime/collections/rt_seq.h (public API),
//        src/runtime/collections/rt_seq_functional.c (map/filter/reduce wrappers)
//
//===----------------------------------------------------------------------===//

#include "rt_seq.h"
#include "rt_box.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_random.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#define SEQ_DEFAULT_CAP 16
#define SEQ_GROWTH_FACTOR 2

/// @brief Internal sequence (dynamic array) implementation structure.
///
/// The Seq is implemented as a growable array that automatically expands when
/// its capacity is exceeded. This provides O(1) amortized append and O(1)
/// random access, making it the most versatile collection type.
///
/// **Memory layout:**
/// ```
/// Seq object (GC-managed):
///   +-----+-----+-------+
///   | len | cap | items |
///   |  5  | 16  | ----->|
///   +-----+-----+---|---+
///                   |
///                   v
/// items array (malloc'd):
///   +---+---+---+---+---+---+---+...+----+
///   | A | B | C | D | E | ? | ? |   | ?  |
///   +---+---+---+---+---+---+---+...+----+
///   [0]  [1] [2] [3] [4]          [cap-1]
///                     ^
///                     | len-1 = last valid index
/// ```
///
/// **Growth strategy:**
/// - Initial capacity: 16 elements
/// - When full, capacity doubles (16 → 32 → 64 → 128 → ...)
/// - This gives O(1) amortized time for Push operations
///
/// **Element ownership:**
/// By default (owns_elements=0), the Seq stores raw pointers and does NOT own
/// the elements. When owns_elements=1, the Seq retains elements on push and
/// releases them on finalize/clear/set-replace, enabling automatic lifetime
/// management via reference counting.
typedef struct rt_seq_impl
{
    int64_t len;          ///< Number of elements currently in the sequence
    int64_t cap;          ///< Current capacity (allocated slots)
    void **items;         ///< Array of element pointers
    int8_t owns_elements; ///< 1 = retain on push, release on finalize/clear
} rt_seq_impl;

/// @brief Finalizer callback invoked when a Seq is garbage collected.
///
/// This function is automatically called by Viper's garbage collector when a
/// Seq object becomes unreachable. It frees the internal items array to
/// prevent memory leaks.
///
/// @param obj Pointer to the Seq object being finalized. May be NULL (no-op).
///
/// @note The Seq does NOT own the elements it contains. Elements are not
///       freed during finalization - they must be managed separately.
/// @note This function is idempotent - safe to call on already-finalized seqs.
///
/// @see rt_seq_clear For removing elements without finalization
/// @brief Release a single element via the object API (safe for strings and objects).
static void seq_release_element(void *val)
{
    if (!val)
        return;
    if (rt_obj_release_check0(val))
        rt_obj_free(val);
}

static void rt_seq_finalize(void *obj)
{
    if (!obj)
        return;
    rt_seq_impl *seq = (rt_seq_impl *)obj;
    if (seq->owns_elements && seq->items)
    {
        for (int64_t i = 0; i < seq->len; i++)
            seq_release_element(seq->items[i]);
    }
    free(seq->items);
    seq->items = NULL;
    seq->len = 0;
    seq->cap = 0;
}

/// @brief Ensures the sequence has capacity for at least `needed` elements.
///
/// If the current capacity is insufficient, the items array is reallocated
/// to a larger size. Growth is exponential (doubling) to amortize allocation
/// costs over many push operations, giving O(1) amortized push complexity.
///
/// **Growth strategy:**
/// - Capacity doubles each time growth is needed
/// - Starting capacity is 16 (SEQ_DEFAULT_CAP)
/// - Growth sequence: 16 → 32 → 64 → 128 → 256 → ...
///
/// @param seq Pointer to the sequence implementation. Must not be NULL.
/// @param needed Minimum required capacity after this call.
///
/// @note Traps on memory allocation failure with "Seq: memory allocation failed".
/// @note Never shrinks the capacity - only grows when needed.
///
/// @see rt_seq_push For the primary user of this function
static void seq_ensure_capacity(rt_seq_impl *seq, int64_t needed)
{
    if (needed <= seq->cap)
        return;

    int64_t new_cap = seq->cap;
    while (new_cap < needed)
    {
        new_cap *= SEQ_GROWTH_FACTOR;
    }

    void **new_items = realloc(seq->items, (size_t)new_cap * sizeof(void *));
    if (!new_items)
    {
        rt_trap("Seq: memory allocation failed");
    }

    seq->items = new_items;
    seq->cap = new_cap;
}

/// @brief Creates a new empty Seq (sequence) with default capacity.
///
/// Allocates and initializes a Seq data structure for storing a dynamic array
/// of elements. The Seq starts with a default capacity of 16 slots and grows
/// automatically as elements are added.
///
/// The Seq is the most versatile Viper collection, providing:
/// - O(1) amortized append (Push)
/// - O(1) random access (Get/Set)
/// - O(n) insertion/removal at arbitrary positions
///
/// **Usage example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("first")
/// seq.Push("second")
/// seq.Push("third")
/// Print seq.Get(0)   ' Outputs: first
/// Print seq.Len()    ' Outputs: 3
/// Print seq.Pop()    ' Outputs: third
/// ```
///
/// @return A pointer to the newly created Seq object. Traps and does not
///         return if memory allocation fails.
///
/// @note Initial capacity is 16 elements (SEQ_DEFAULT_CAP).
/// @note The Seq does not own the elements stored in it - they must be
///       managed separately by the caller.
/// @note Thread safety: Not thread-safe. External synchronization required.
///
/// @see rt_seq_with_capacity For creating with a specific initial capacity
/// @see rt_seq_push For adding elements
/// @see rt_seq_get For accessing elements
/// @see rt_seq_finalize For cleanup behavior
void *rt_seq_new(void)
{
    rt_seq_impl *seq = (rt_seq_impl *)rt_obj_new_i64(RT_SEQ_CLASS_ID, (int64_t)sizeof(rt_seq_impl));
    if (!seq)
    {
        rt_trap("Seq: memory allocation failed");
    }

    seq->len = 0;
    seq->cap = SEQ_DEFAULT_CAP;
    seq->items = malloc((size_t)SEQ_DEFAULT_CAP * sizeof(void *));
    rt_obj_set_finalizer(seq, rt_seq_finalize);

    if (!seq->items)
    {
        if (rt_obj_release_check0(seq))
            rt_obj_free(seq);
        rt_trap("Seq: memory allocation failed");
    }

    return seq;
}

/// @brief Creates a new empty Seq with a specified initial capacity.
///
/// Allocates a Seq with pre-allocated space for the specified number of elements.
/// This is useful when you know approximately how many elements you'll need,
/// as it avoids the overhead of multiple reallocations during growth.
///
/// **Performance optimization:**
/// If you know you'll be adding 1000 elements, creating a Seq with capacity 1000
/// avoids the growth sequence: 16 → 32 → 64 → 128 → 256 → 512 → 1024, saving
/// 6 reallocations and memory copies.
///
/// **Example:**
/// ```
/// ' Pre-allocate for 100 elements
/// Dim scores = Seq.WithCapacity(100)
/// For i = 1 To 100
///     scores.Push(GetScore(i))  ' No reallocations occur
/// Next
/// ```
///
/// @param cap Initial capacity. Values less than 1 are clamped to 1.
///
/// @return A pointer to the newly created Seq object. Traps and does not
///         return if memory allocation fails.
///
/// @note The Seq is empty after creation (length 0) - capacity is just reserved space.
/// @note The Seq does not own the elements stored in it.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_new For creating with default capacity (16)
/// @see rt_seq_cap For querying the current capacity
void *rt_seq_with_capacity(int64_t cap)
{
    if (cap < 1)
        cap = 1;

    rt_seq_impl *seq = (rt_seq_impl *)rt_obj_new_i64(RT_SEQ_CLASS_ID, (int64_t)sizeof(rt_seq_impl));
    if (!seq)
    {
        rt_trap("Seq: memory allocation failed");
    }

    seq->len = 0;
    seq->cap = cap;
    seq->items = malloc((size_t)cap * sizeof(void *));
    rt_obj_set_finalizer(seq, rt_seq_finalize);

    if (!seq->items)
    {
        if (rt_obj_release_check0(seq))
            rt_obj_free(seq);
        rt_trap("Seq: memory allocation failed");
    }

    return seq;
}

/// @brief Enable or disable element ownership for a Seq.
///
/// When owns_elements=1, the Seq retains elements on push/set and releases
/// them on clear/finalize. When owns_elements=0 (default), the Seq stores
/// raw pointers and the caller manages element lifetime.
///
/// @param obj Pointer to a Seq object. Must not be NULL.
/// @param owns 1 to enable ownership, 0 to disable.
///
/// @note Must be called before any elements are pushed. Changing ownership
///       mode on a non-empty Seq may cause leaks or double-frees.
void rt_seq_set_owns_elements(void *obj, int8_t owns)
{
    if (!obj)
        return;
    ((rt_seq_impl *)obj)->owns_elements = owns;
}

/// @brief Returns the number of elements currently in the Seq.
///
/// This function returns how many elements have been added and not yet removed.
/// The count is maintained internally and returned in O(1) time.
///
/// @param obj Pointer to a Seq object. If NULL, returns 0.
///
/// @return The number of elements in the Seq (>= 0). Returns 0 if obj is NULL.
///
/// @note O(1) time complexity.
///
/// @see rt_seq_cap For the allocated capacity
/// @see rt_seq_is_empty For a boolean check
int64_t rt_seq_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_seq_impl *)obj)->len;
}

/// @brief Returns the current allocated capacity of the Seq.
///
/// Capacity is the number of elements the Seq can hold without reallocating.
/// This is always >= the current length. When length exceeds capacity during
/// a push, the Seq automatically grows (capacity doubles).
///
/// **Capacity vs Length:**
/// - Length: How many elements are currently stored
/// - Capacity: How many elements can be stored without reallocation
///
/// @param obj Pointer to a Seq object. If NULL, returns 0.
///
/// @return The current capacity (>= 0). Returns 0 if obj is NULL.
///
/// @note O(1) time complexity.
///
/// @see rt_seq_len For the number of actual elements
/// @see rt_seq_with_capacity For pre-allocating capacity
int64_t rt_seq_cap(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_seq_impl *)obj)->cap;
}

/// @brief Checks whether the Seq contains no elements.
///
/// A Seq is considered empty when its length is 0, which occurs:
/// - Immediately after creation
/// - After all elements have been popped/removed
/// - After calling rt_seq_clear
///
/// @param obj Pointer to a Seq object. If NULL, returns true (1).
///
/// @return 1 (true) if the Seq is empty or obj is NULL, 0 (false) otherwise.
///
/// @note O(1) time complexity.
///
/// @see rt_seq_len For the exact count
/// @see rt_seq_clear For removing all elements
int8_t rt_seq_is_empty(void *obj)
{
    if (!obj)
        return 1;
    return ((rt_seq_impl *)obj)->len == 0 ? 1 : 0;
}

/// @brief Returns the element at the specified index.
///
/// Provides O(1) random access to any element in the Seq. Indices are
/// zero-based, so valid indices range from 0 to len-1.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("b")
/// seq.Push("c")
/// Print seq.Get(0)  ' Outputs: a
/// Print seq.Get(1)  ' Outputs: b
/// Print seq.Get(2)  ' Outputs: c
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
/// @param idx Zero-based index of the element to retrieve (0 to len-1).
///
/// @return The element at the specified index.
///
/// @note O(1) time complexity.
/// @note Traps with "Seq.Get: null sequence" if obj is NULL.
/// @note Traps with "Seq.Get: index out of bounds" if idx < 0 or idx >= len.
/// @note The Seq retains ownership - the returned pointer is valid as long as
///       the element remains in the Seq.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_set For modifying an element
/// @see rt_seq_first For getting the first element
/// @see rt_seq_last For getting the last element
void *rt_seq_get(void *obj, int64_t idx)
{
    if (!obj)
        rt_trap("Seq.Get: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (idx < 0 || idx >= seq->len)
    {
        rt_trap("Seq.Get: index out of bounds");
    }

    return seq->items[idx];
}

/// @brief Get string element at index from a string sequence.
///
/// All seq<str>-returning runtime functions store raw rt_string pointers
/// directly in the sequence (not boxed). This helper casts the void* element
/// to rt_string without any boxing/unboxing, for use in for-in iteration.
///
/// @param obj Opaque Seq object pointer.
/// @param idx Index of element to retrieve.
/// @return String element at the index (raw rt_string pointer).
struct rt_string_impl *rt_seq_get_str(void *obj, int64_t idx)
{
    return (struct rt_string_impl *)rt_seq_get(obj, idx);
}

/// @brief Replaces the element at the specified index.
///
/// Provides O(1) random modification of any element in the Seq. The index
/// must refer to an existing element - this function cannot extend the Seq.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("b")
/// seq.Set(0, "x")
/// Print seq.Get(0)  ' Outputs: x
/// Print seq.Get(1)  ' Outputs: b
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
/// @param idx Zero-based index of the element to modify (0 to len-1).
/// @param val The new value to store at this index. May be NULL.
///
/// @note O(1) time complexity.
/// @note The Seq does not take ownership of val - the caller manages its lifetime.
/// @note Traps with "Seq.Set: null sequence" if obj is NULL.
/// @note Traps with "Seq.Set: index out of bounds" if idx < 0 or idx >= len.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_get For reading an element
/// @see rt_seq_push For adding new elements
void rt_seq_set(void *obj, int64_t idx, void *val)
{
    if (!obj)
        rt_trap("Seq.Set: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (idx < 0 || idx >= seq->len)
    {
        rt_trap("Seq.Set: index out of bounds");
    }

    if (seq->owns_elements)
    {
        if (val)
            rt_obj_retain_maybe(val);
        seq_release_element(seq->items[idx]);
    }
    seq->items[idx] = val;
}

/// @brief Adds an element to the end of the Seq.
///
/// Appends a new element after the current last element. This is the primary
/// way to grow a Seq. If capacity is exceeded, the Seq automatically doubles
/// its internal storage.
///
/// **Visual example:**
/// ```
/// Before Push(D):  [A, B, C]      len=3
/// After Push(D):   [A, B, C, D]   len=4
/// ```
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("first")
/// seq.Push("second")
/// seq.Push("third")
/// Print seq.Len()  ' Outputs: 3
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
/// @param val The element to add. May be NULL (NULL is a valid element).
///
/// @note O(1) amortized time complexity. Occasional O(n) when resizing occurs.
/// @note The Seq does not take ownership of val - the caller manages its lifetime.
/// @note Traps with "Seq.Push: null sequence" if obj is NULL.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_pop For removing from the end
/// @see rt_seq_insert For inserting at arbitrary positions
/// @see rt_seq_push_all For appending multiple elements
void rt_seq_push(void *obj, void *val)
{
    if (!obj)
        rt_trap("Seq.Push: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    seq_ensure_capacity(seq, seq->len + 1);
    if (seq->owns_elements && val)
        rt_obj_retain_maybe(val);
    seq->items[seq->len] = val;
    seq->len++;
}

void rt_seq_push_raw(void *obj, void *val)
{
    if (!obj)
        rt_trap("Seq.Push: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    seq_ensure_capacity(seq, seq->len + 1);
    seq->items[seq->len] = val;
    seq->len++;
}

/// @brief Appends all elements from another Seq to the end of this Seq.
///
/// Copies all elements from the source Seq and appends them to the destination
/// Seq, preserving their order. This is more efficient than pushing elements
/// one by one as it performs a single capacity check and memory copy.
///
/// **Example:**
/// ```
/// Dim seq1 = Seq.New()
/// seq1.Push("a")
/// seq1.Push("b")
///
/// Dim seq2 = Seq.New()
/// seq2.Push("c")
/// seq2.Push("d")
///
/// seq1.PushAll(seq2)
/// ' seq1 is now: [a, b, c, d]
/// ' seq2 is unchanged: [c, d]
/// ```
///
/// **Self-append behavior:**
/// When pushing a Seq onto itself (obj == other), the Seq doubles its contents.
/// This is handled specially to avoid infinite loops:
/// ```
/// Dim seq = Seq.New()
/// seq.Push("x")
/// seq.PushAll(seq)  ' seq becomes: [x, x]
/// ```
///
/// @param obj Destination Seq to append to. Must not be NULL.
/// @param other Source Seq whose elements will be copied. If NULL, no-op.
///
/// @note O(n) time complexity where n is the length of other.
/// @note The source Seq is not modified (elements are copied, not moved).
/// @note Traps with "Seq.PushAll: null sequence" if obj is NULL.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_push For adding single elements
/// @see rt_seq_clone For creating a copy of a Seq
void rt_seq_push_all(void *obj, void *other)
{
    if (!obj)
        rt_trap("Seq.PushAll: null sequence");
    if (!other)
        return;

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    rt_seq_impl *src = (rt_seq_impl *)other;

    if (src->len <= 0)
        return;

    if (seq == src)
    {
        int64_t original_len = seq->len;
        seq_ensure_capacity(seq, original_len + original_len);
        memcpy(&seq->items[original_len], seq->items, (size_t)original_len * sizeof(void *));
        if (seq->owns_elements)
        {
            for (int64_t i = 0; i < original_len; i++)
                if (seq->items[i])
                    rt_obj_retain_maybe(seq->items[i]);
        }
        seq->len = original_len + original_len;
        return;
    }

    seq_ensure_capacity(seq, seq->len + src->len);
    memcpy(&seq->items[seq->len], src->items, (size_t)src->len * sizeof(void *));
    if (seq->owns_elements)
    {
        for (int64_t i = 0; i < src->len; i++)
            if (src->items[i])
                rt_obj_retain_maybe(src->items[i]);
    }
    seq->len += src->len;
}

/// @brief Removes and returns the last element from the Seq.
///
/// Removes the element at the end of the Seq and returns it. This is the
/// inverse of Push and provides O(1) removal from the end.
///
/// **Visual example:**
/// ```
/// Before Pop():  [A, B, C, D]   len=4
/// After Pop():   [A, B, C]      len=3
/// Returns: D
/// ```
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("first")
/// seq.Push("second")
/// seq.Push("third")
/// Print seq.Pop()  ' Outputs: third
/// Print seq.Pop()  ' Outputs: second
/// Print seq.Len()  ' Outputs: 1
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
///
/// @return The element that was at the end of the Seq.
///
/// @note O(1) time complexity.
/// @note The Seq releases its reference - the caller now owns the element.
/// @note Traps with "Seq.Pop: null sequence" if obj is NULL.
/// @note Traps with "Seq.Pop: sequence is empty" if the Seq is empty.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_push For the inverse operation
/// @see rt_seq_peek For viewing without removing
/// @see rt_seq_is_empty For checking before pop
void *rt_seq_pop(void *obj)
{
    if (!obj)
        rt_trap("Seq.Pop: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
    {
        rt_trap("Seq.Pop: sequence is empty");
    }

    seq->len--;
    return seq->items[seq->len];
}

/// @brief Returns the last element without removing it.
///
/// Peeks at the element at the end of the Seq without modifying the Seq.
/// This is equivalent to Get(Len() - 1) but more convenient and descriptive.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("b")
/// Print seq.Peek()  ' Outputs: b
/// Print seq.Peek()  ' Outputs: b (still there)
/// Print seq.Pop()   ' Outputs: b (now removed)
/// Print seq.Peek()  ' Outputs: a
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
///
/// @return The element at the end of the Seq (not removed).
///
/// @note O(1) time complexity.
/// @note The Seq retains ownership - the returned pointer is valid as long as
///       the element remains in the Seq.
/// @note Traps with "Seq.Peek: null sequence" if obj is NULL.
/// @note Traps with "Seq.Peek: sequence is empty" if the Seq is empty.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_pop For removing while retrieving
/// @see rt_seq_last Alias for this function
/// @see rt_seq_first For viewing the first element
void *rt_seq_peek(void *obj)
{
    if (!obj)
        rt_trap("Seq.Peek: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
    {
        rt_trap("Seq.Peek: sequence is empty");
    }

    return seq->items[seq->len - 1];
}

/// @brief Returns the first element without removing it.
///
/// Provides convenient access to the element at index 0. This is equivalent
/// to Get(0) but more descriptive.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("b")
/// seq.Push("c")
/// Print seq.First()  ' Outputs: a
/// Print seq.Last()   ' Outputs: c
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
///
/// @return The element at index 0 (not removed).
///
/// @note O(1) time complexity.
/// @note The Seq retains ownership - the returned pointer is valid as long as
///       the element remains in the Seq.
/// @note Traps with "Seq.First: null sequence" if obj is NULL.
/// @note Traps with "Seq.First: sequence is empty" if the Seq is empty.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_last For viewing the last element
/// @see rt_seq_get For accessing by arbitrary index
void *rt_seq_first(void *obj)
{
    if (!obj)
        rt_trap("Seq.First: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
    {
        rt_trap("Seq.First: sequence is empty");
    }

    return seq->items[0];
}

/// @brief Returns the last element without removing it.
///
/// Provides convenient access to the element at index (len - 1). This is
/// equivalent to Get(Len() - 1) and Peek() but with a more descriptive name.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("b")
/// seq.Push("c")
/// Print seq.Last()   ' Outputs: c
/// Print seq.First()  ' Outputs: a
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
///
/// @return The element at index (len - 1) (not removed).
///
/// @note O(1) time complexity.
/// @note The Seq retains ownership - the returned pointer is valid as long as
///       the element remains in the Seq.
/// @note Traps with "Seq.Last: null sequence" if obj is NULL.
/// @note Traps with "Seq.Last: sequence is empty" if the Seq is empty.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_first For viewing the first element
/// @see rt_seq_peek Alias for this function
/// @see rt_seq_get For accessing by arbitrary index
void *rt_seq_last(void *obj)
{
    if (!obj)
        rt_trap("Seq.Last: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
    {
        rt_trap("Seq.Last: sequence is empty");
    }

    return seq->items[seq->len - 1];
}

/// @brief Inserts an element at the specified position.
///
/// Inserts a new element at the given index, shifting all subsequent elements
/// one position to the right. Unlike Set, Insert grows the Seq by one element.
///
/// **Visual example:**
/// ```
/// Before Insert(1, X):  [A, B, C]      len=3
/// After Insert(1, X):   [A, X, B, C]   len=4
/// ```
///
/// **Valid indices:**
/// - 0: Insert at the beginning (before all elements)
/// - len: Insert at the end (equivalent to Push)
/// - Any value from 0 to len (inclusive)
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("c")
/// seq.Insert(1, "b")    ' Insert between a and c
/// ' seq is now: [a, b, c]
/// seq.Insert(0, "start") ' Insert at beginning
/// ' seq is now: [start, a, b, c]
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
/// @param idx Position to insert at (0 to len inclusive).
/// @param val The element to insert. May be NULL.
///
/// @note O(n) time complexity due to element shifting.
/// @note The Seq does not take ownership of val.
/// @note Traps with "Seq.Insert: null sequence" if obj is NULL.
/// @note Traps with "Seq.Insert: index out of bounds" if idx < 0 or idx > len.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_push For appending to the end (O(1))
/// @see rt_seq_remove For removing at an index
void rt_seq_insert(void *obj, int64_t idx, void *val)
{
    if (!obj)
        rt_trap("Seq.Insert: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (idx < 0 || idx > seq->len)
    {
        rt_trap("Seq.Insert: index out of bounds");
    }

    seq_ensure_capacity(seq, seq->len + 1);

    // Shift elements to the right
    if (idx < seq->len)
    {
        memmove(&seq->items[idx + 1], &seq->items[idx], (size_t)(seq->len - idx) * sizeof(void *));
    }

    if (seq->owns_elements && val)
        rt_obj_retain_maybe(val);
    seq->items[idx] = val;
    seq->len++;
}

/// @brief Removes and returns the element at the specified position.
///
/// Removes the element at the given index and shifts all subsequent elements
/// one position to the left to fill the gap.
///
/// **Visual example:**
/// ```
/// Before Remove(1):  [A, B, C, D]   len=4
/// After Remove(1):   [A, C, D]      len=3
/// Returns: B
/// ```
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("b")
/// seq.Push("c")
/// Print seq.Remove(1)  ' Outputs: b
/// ' seq is now: [a, c]
/// Print seq.Remove(0)  ' Outputs: a
/// ' seq is now: [c]
/// ```
///
/// @param obj Pointer to a Seq object. Must not be NULL.
/// @param idx Zero-based index of the element to remove (0 to len-1).
///
/// @return The element that was removed.
///
/// @note O(n) time complexity due to element shifting.
/// @note The Seq releases its reference - the caller now owns the element.
/// @note Traps with "Seq.Remove: null sequence" if obj is NULL.
/// @note Traps with "Seq.Remove: index out of bounds" if idx < 0 or idx >= len.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_pop For removing from the end (O(1))
/// @see rt_seq_insert For inserting at an index
void *rt_seq_remove(void *obj, int64_t idx)
{
    if (!obj)
        rt_trap("Seq.Remove: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (idx < 0 || idx >= seq->len)
    {
        rt_trap("Seq.Remove: index out of bounds");
    }

    void *val = seq->items[idx];

    // Shift elements to the left
    if (idx < seq->len - 1)
    {
        memmove(
            &seq->items[idx], &seq->items[idx + 1], (size_t)(seq->len - idx - 1) * sizeof(void *));
    }

    seq->len--;
    return val;
}

/// @brief Removes all elements from the Seq.
///
/// Clears the Seq by resetting its length to 0. The capacity remains unchanged
/// (no memory is freed), allowing the Seq to be efficiently reused for new
/// elements.
///
/// **After clear:**
/// - Length becomes 0
/// - is_empty returns true
/// - Capacity unchanged (no reallocation)
/// - All element references are forgotten (not freed)
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("b")
/// Print seq.Len()    ' Outputs: 2
/// seq.Clear()
/// Print seq.Len()    ' Outputs: 0
/// Print seq.IsEmpty  ' Outputs: True
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
///
/// @note O(1) time complexity - just resets the length counter.
/// @note The Seq does NOT free the elements - they must be managed separately.
/// @note The internal array is not zeroed - old pointers remain but are
///       inaccessible through the public API.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_finalize For complete cleanup (including the array)
/// @see rt_seq_is_empty For checking if empty
void rt_seq_clear(void *obj)
{
    if (!obj)
        return;
    rt_seq_impl *seq = (rt_seq_impl *)obj;
    if (seq->owns_elements && seq->items)
    {
        for (int64_t i = 0; i < seq->len; i++)
            seq_release_element(seq->items[i]);
    }
    seq->len = 0;
}

/// @brief Finds the first occurrence of an element in the Seq.
///
/// Searches for an element by pointer equality (identity comparison, not
/// value equality). Returns the index of the first match, or -1 if not found.
///
/// **Comparison semantics:**
/// This function compares pointers, not values. Two strings with the same
/// content but different memory addresses will NOT match. For value-based
/// comparison, iterate manually and compare values.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// Dim obj1 = SomeObject.New()
/// Dim obj2 = SomeObject.New()
/// seq.Push(obj1)
/// seq.Push(obj2)
/// Print seq.Find(obj1)   ' Outputs: 0
/// Print seq.Find(obj2)   ' Outputs: 1
/// Print seq.Find(Nothing) ' Outputs: -1 (not found)
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns -1.
/// @param val The element to search for (compared by content for boxed values).
///
/// @return The zero-based index of the first occurrence, or -1 if not found
///         or obj is NULL.
///
/// @note O(n) time complexity - linear search from the beginning.
/// @note Boxed values are compared by content; non-boxed by pointer identity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_has For boolean membership check
int64_t rt_seq_find(void *obj, void *val)
{
    if (!obj)
        return -1;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    for (int64_t i = 0; i < seq->len; i++)
    {
        if (rt_box_equal(seq->items[i], val))
        {
            return i;
        }
    }

    return -1;
}

/// @brief Checks whether the Seq contains a specific element.
///
/// Tests if the element is present in the Seq using content-aware equality.
/// This is a convenience wrapper around rt_seq_find that returns a boolean.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// Dim obj = SomeObject.New()
/// seq.Push(obj)
/// Print seq.Has(obj)     ' Outputs: True
/// Print seq.Has(Nothing) ' Outputs: False
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 0 (false).
/// @param val The element to search for (compared by content for boxed values).
///
/// @return 1 (true) if the element is found, 0 (false) otherwise.
///
/// @note O(n) time complexity - linear search.
/// @note Boxed values are compared by content; non-boxed by pointer identity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_find For getting the index of the element
int8_t rt_seq_has(void *obj, void *val)
{
    return rt_seq_find(obj, val) >= 0 ? 1 : 0;
}

/// @brief Reverses the order of elements in the Seq in place.
///
/// Modifies the Seq so that elements appear in reverse order. The first
/// element becomes the last, the second becomes second-to-last, and so on.
///
/// **Visual example:**
/// ```
/// Before Reverse():  [A, B, C, D]
/// After Reverse():   [D, C, B, A]
/// ```
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push(1)
/// seq.Push(2)
/// seq.Push(3)
/// seq.Reverse()
/// ' seq is now: [3, 2, 1]
/// For i = 0 To seq.Len() - 1
///     Print seq.Get(i)  ' Outputs: 3, 2, 1
/// Next
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
///
/// @note O(n/2) time complexity - swaps pairs from ends toward middle.
/// @note Modifies the Seq in place (no new allocation).
/// @note Safe to call on empty or single-element Seqs (no-op).
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_shuffle For randomizing order
void rt_seq_reverse(void *obj)
{
    if (!obj)
        return;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    for (int64_t i = 0; i < seq->len / 2; i++)
    {
        int64_t j = seq->len - 1 - i;
        void *tmp = seq->items[i];
        seq->items[i] = seq->items[j];
        seq->items[j] = tmp;
    }
}

/// @brief Randomly shuffles the elements in the Seq in place.
///
/// Rearranges the elements into a random permutation using the Fisher-Yates
/// (also known as Knuth) shuffle algorithm. Each possible permutation has
/// equal probability.
///
/// **Fisher-Yates algorithm:**
/// For each position i from len-1 down to 1:
/// 1. Pick a random index j from 0 to i (inclusive)
/// 2. Swap elements at positions i and j
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push(1)
/// seq.Push(2)
/// seq.Push(3)
/// seq.Push(4)
/// seq.Shuffle()
/// ' seq might now be: [3, 1, 4, 2] (random order)
/// ```
///
/// **Deterministic shuffles:**
/// To get reproducible shuffles, seed the random number generator before
/// calling Shuffle:
/// ```
/// Random.Seed(12345)
/// seq.Shuffle()  ' Same seed = same shuffle result
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
///
/// @note O(n) time complexity.
/// @note Modifies the Seq in place (no new allocation).
/// @note Uses Viper.Random.NextInt for randomness - seed for reproducibility.
/// @note Safe to call on empty or single-element Seqs (no-op).
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_reverse For reversing order
void rt_seq_shuffle(void *obj)
{
    if (!obj)
        return;

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    if (seq->len <= 1)
        return;

    for (int64_t i = seq->len - 1; i > 0; --i)
    {
        int64_t j = (int64_t)rt_rand_int((long long)(i + 1));
        void *tmp = seq->items[i];
        seq->items[i] = seq->items[j];
        seq->items[j] = tmp;
    }
}

/// @brief Creates a new Seq containing a subset of elements from [start, end).
///
/// Extracts a portion of the Seq into a new Seq. The range is half-open:
/// start is inclusive, end is exclusive. Out-of-bounds indices are clamped
/// to valid ranges rather than causing errors.
///
/// **Visual example:**
/// ```
/// Original:            [A, B, C, D, E]
/// Slice(1, 4):         [B, C, D]
/// Slice(0, 2):         [A, B]
/// Slice(3, 100):       [D, E]  (end clamped to 5)
/// ```
///
/// **Index clamping:**
/// - start < 0 is treated as 0
/// - end > len is treated as len
/// - start >= end returns an empty Seq
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("a")
/// seq.Push("b")
/// seq.Push("c")
/// seq.Push("d")
/// Dim sub = seq.Slice(1, 3)
/// ' sub is: [b, c]
/// ' original seq is unchanged
/// ```
///
/// @param obj Source Seq to slice from. If NULL, returns an empty Seq.
/// @param start Start index (inclusive). Clamped to 0 if negative.
/// @param end End index (exclusive). Clamped to len if greater.
///
/// @return A new Seq containing elements from indices [start, end).
///         Returns empty Seq if start >= end or obj is NULL.
///
/// @note O(n) time complexity where n is the slice length.
/// @note The source Seq is not modified.
/// @note Elements are shallow-copied (pointers, not deep copies).
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_clone For copying the entire Seq
void *rt_seq_slice(void *obj, int64_t start, int64_t end)
{
    if (!obj)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    // Clamp bounds
    if (start < 0)
        start = 0;
    if (end > seq->len)
        end = seq->len;
    if (start >= end)
    {
        return rt_seq_new();
    }

    int64_t new_len = end - start;
    rt_seq_impl *result = rt_seq_with_capacity(new_len);

    memcpy(result->items, &seq->items[start], (size_t)new_len * sizeof(void *));
    ((rt_seq_impl *)result)->len = new_len;

    return result;
}

/// @brief Creates a shallow copy of the Seq.
///
/// Returns a new Seq containing all elements from the original. This is a
/// shallow copy: the element pointers are copied, but the elements themselves
/// are not duplicated. Both Seqs will point to the same underlying objects.
///
/// **Shallow vs Deep copy:**
/// - Shallow (this function): Copies pointers, shares objects
/// - Deep: Would copy objects too (not provided)
///
/// **Example:**
/// ```
/// Dim original = Seq.New()
/// original.Push("a")
/// original.Push("b")
/// original.Push("c")
///
/// Dim copy = original.Clone()
/// ' copy is: [a, b, c]
///
/// copy.Push("d")
/// ' copy is: [a, b, c, d]
/// ' original is: [a, b, c] (unchanged)
/// ```
///
/// **Use cases:**
/// - Creating a backup before modifications
/// - Passing a copy to a function that might modify it
/// - Testing with a duplicate while preserving the original
///
/// @param obj Source Seq to copy. If NULL, returns an empty Seq.
///
/// @return A new Seq containing the same elements as the original.
///
/// @note O(n) time complexity where n is the length.
/// @note The source Seq is not modified.
/// @note Elements are shallow-copied (same pointers as original).
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_slice For copying a subset
void *rt_seq_clone(void *obj)
{
    if (!obj)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    return rt_seq_slice(obj, 0, seq->len);
}

//=============================================================================
// Sorting Implementation
//=============================================================================

// Forward declaration for rt_seq_sort_by (used by rt_seq_sort)
void rt_seq_sort_by(void *obj, int64_t (*cmp)(void *, void *));

/// @brief Default comparison function for sorting.
/// @details Compares elements as strings if they appear to be strings,
///          otherwise compares by pointer value. String comparison is
///          case-sensitive and lexicographic.
/// @param a First element pointer.
/// @param b Second element pointer.
/// @return Negative if a < b, zero if equal, positive if a > b.
static int64_t seq_default_compare(void *a, void *b)
{
    // If both are NULL, they're equal
    if (!a && !b)
        return 0;
    // NULL sorts before non-NULL
    if (!a)
        return -1;
    if (!b)
        return 1;

    // Check if elements are strings using the runtime string checker
    if (rt_string_is_handle(a) && rt_string_is_handle(b))
    {
        return rt_str_cmp((rt_string)a, (rt_string)b);
    }

    // Fall back to pointer comparison for non-strings
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

/// @brief Merge two sorted halves of an array.
/// @details Used by merge sort. Merges items[left..mid] and items[mid+1..right]
///          into a sorted sequence.
/// @param items Array of element pointers.
/// @param temp Temporary buffer for merging.
/// @param left Start index of left half.
/// @param mid End index of left half.
/// @param right End index of right half.
/// @param cmp Comparison function.
static void seq_merge(void **items,
                      void **temp,
                      int64_t left,
                      int64_t mid,
                      int64_t right,
                      int64_t (*cmp)(void *, void *))
{
    int64_t i = left;
    int64_t j = mid + 1;
    int64_t k = left;

    // Merge the two halves
    while (i <= mid && j <= right)
    {
        if (cmp(items[i], items[j]) <= 0)
        {
            temp[k++] = items[i++];
        }
        else
        {
            temp[k++] = items[j++];
        }
    }

    // Copy remaining elements from left half
    while (i <= mid)
    {
        temp[k++] = items[i++];
    }

    // Copy remaining elements from right half
    while (j <= right)
    {
        temp[k++] = items[j++];
    }

    // Copy back to original array
    for (int64_t x = left; x <= right; x++)
    {
        items[x] = temp[x];
    }
}

/// @brief Recursive merge sort implementation.
/// @param items Array of element pointers.
/// @param temp Temporary buffer.
/// @param left Start index.
/// @param right End index (inclusive).
/// @param cmp Comparison function.
static void seq_merge_sort(
    void **items, void **temp, int64_t left, int64_t right, int64_t (*cmp)(void *, void *))
{
    if (left >= right)
        return;

    int64_t mid = left + (right - left) / 2;
    seq_merge_sort(items, temp, left, mid, cmp);
    seq_merge_sort(items, temp, mid + 1, right, cmp);
    seq_merge(items, temp, left, mid, right, cmp);
}

/// @brief Sorts the elements in the Seq in ascending order.
///
/// Rearranges elements into ascending order using a stable merge sort algorithm.
/// Strings are compared lexicographically (case-sensitive). Non-string objects
/// are compared by their memory address (pointer value).
///
/// **Sorting behavior:**
/// - Strings: Lexicographic comparison ("a" < "b" < "z")
/// - Other objects: Pointer comparison (for consistent ordering)
/// - NULL values sort before non-NULL values
///
/// **Stability:**
/// The sort is stable, meaning equal elements maintain their relative order.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("cherry")
/// seq.Push("apple")
/// seq.Push("banana")
/// seq.Sort()
/// ' seq is now: ["apple", "banana", "cherry"]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
///
/// @note O(n log n) time complexity.
/// @note O(n) additional space for the merge operation.
/// @note Modifies the Seq in place.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_sort_desc For descending order
/// @see rt_seq_sort_by For custom comparison
void rt_seq_sort(void *obj)
{
    rt_seq_sort_by(obj, seq_default_compare);
}

/// @brief Sorts the elements using a custom comparison function.
///
/// Rearranges elements into order determined by the provided comparison function.
/// The comparison function receives two element pointers and should return:
/// - Negative value if the first element should come before the second
/// - Zero if the elements are equal (stable sort preserves order)
/// - Positive value if the first element should come after the second
///
/// **Example with numbers (boxed):**
/// ```
/// Function CompareNumbers(a, b) As I64
///     Dim na = Unbox.I64(a)
///     Dim nb = Unbox.I64(b)
///     Return na - nb
/// End Function
///
/// Dim seq = Seq.New()
/// seq.Push(Box.I64(42))
/// seq.Push(Box.I64(17))
/// seq.Push(Box.I64(99))
/// seq.SortBy(AddressOf CompareNumbers)
/// ' seq is now: [17, 42, 99]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
/// @param cmp Comparison function. If NULL, uses default string/pointer comparison.
///
/// @note O(n log n) time complexity.
/// @note O(n) additional space for the merge operation.
/// @note Modifies the Seq in place.
/// @note The comparison function must be consistent (transitive ordering).
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_sort For default ascending sort
void rt_seq_sort_by(void *obj, int64_t (*cmp)(void *, void *))
{
    if (!obj)
        return;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    // Nothing to sort
    if (seq->len <= 1)
        return;

    // Use default comparison if none provided
    if (!cmp)
        cmp = seq_default_compare;

    // Allocate temporary buffer for merge sort
    void **temp = (void **)malloc((size_t)seq->len * sizeof(void *));
    if (!temp)
    {
        rt_trap("Seq.Sort: memory allocation failed");
        return;
    }

    // Perform stable merge sort
    seq_merge_sort(seq->items, temp, 0, seq->len - 1, cmp);

    free(temp);
}

/// @brief Comparison function for descending sort.
static int64_t seq_compare_desc(void *a, void *b)
{
    return -seq_default_compare(a, b);
}

/// @brief Sorts the elements in the Seq in descending order.
///
/// Rearranges elements into descending order using a stable merge sort algorithm.
/// This is equivalent to calling Sort() followed by Reverse(), but more efficient.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("apple")
/// seq.Push("cherry")
/// seq.Push("banana")
/// seq.SortDesc()
/// ' seq is now: ["cherry", "banana", "apple"]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
///
/// @note O(n log n) time complexity.
/// @note O(n) additional space for the merge operation.
/// @note Modifies the Seq in place.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_sort For ascending order
void rt_seq_sort_desc(void *obj)
{
    rt_seq_sort_by(obj, seq_compare_desc);
}

//=============================================================================
// Functional Operations
//=============================================================================

/// @brief Create a new Seq containing only elements matching a predicate.
///
/// Iterates through the Seq and includes elements for which the predicate
/// function returns non-zero (true). This is the primary filtering operation.
///
/// **Example:**
/// ```
/// Function IsEven(n) As Bool
///     Return Unbox.I64(n) Mod 2 = 0
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// nums.Push(Box.I64(3))
/// nums.Push(Box.I64(4))
/// Dim evens = nums.Keep(AddressOf IsEven)
/// ' evens is: [2, 4]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param pred Predicate function returning non-zero to include element.
///             If NULL, returns a clone of the original Seq.
///
/// @return New Seq containing only matching elements.
///
/// @note O(n) time complexity.
/// @note Creates a new Seq; original is not modified.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_reject For the inverse operation
void *rt_seq_keep(void *obj, int8_t (*pred)(void *))
{
    if (!obj)
        return rt_seq_new();

    if (!pred)
        return rt_seq_clone(obj);

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *result = rt_seq_new();

    for (int64_t i = 0; i < seq->len; i++)
    {
        if (pred(seq->items[i]))
        {
            rt_seq_push(result, seq->items[i]);
        }
    }

    return result;
}

/// @brief Create a new Seq excluding elements matching a predicate.
///
/// Inverse of Keep. Includes elements for which the predicate returns zero (false).
///
/// **Example:**
/// ```
/// Function IsEmpty(s) As Bool
///     Return Len(s) = 0
/// End Function
///
/// Dim words = Seq.New()
/// words.Push("hello")
/// words.Push("")
/// words.Push("world")
/// words.Push("")
/// Dim nonEmpty = words.Reject(AddressOf IsEmpty)
/// ' nonEmpty is: ["hello", "world"]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param pred Predicate function. Elements where this returns non-zero are excluded.
///             If NULL, returns a clone of the original Seq.
///
/// @return New Seq containing only non-matching elements.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_keep For the inverse operation
void *rt_seq_reject(void *obj, int8_t (*pred)(void *))
{
    if (!obj)
        return rt_seq_new();

    if (!pred)
        return rt_seq_clone(obj);

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *result = rt_seq_new();

    for (int64_t i = 0; i < seq->len; i++)
    {
        if (!pred(seq->items[i]))
        {
            rt_seq_push(result, seq->items[i]);
        }
    }

    return result;
}

/// @brief Create a new Seq by transforming each element with a function.
///
/// Applies the transform function to each element and collects the results
/// into a new Seq. This is the primary mapping operation.
///
/// **Example:**
/// ```
/// Function Double(n) As Object
///     Return Box.I64(Unbox.I64(n) * 2)
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// nums.Push(Box.I64(3))
/// Dim doubled = nums.Apply(AddressOf Double)
/// ' doubled is: [2, 4, 6]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param fn Transform function. If NULL, returns a clone of the original.
///
/// @return New Seq containing transformed elements.
///
/// @note O(n) time complexity.
/// @note The transform function must return a valid object pointer.
/// @note Thread safety: Not thread-safe.
void *rt_seq_apply(void *obj, void *(*fn)(void *))
{
    if (!obj)
        return rt_seq_new();

    if (!fn)
        return rt_seq_clone(obj);

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *result = rt_seq_with_capacity(seq->len);

    for (int64_t i = 0; i < seq->len; i++)
    {
        rt_seq_push(result, fn(seq->items[i]));
    }

    return result;
}

/// @brief Check if all elements satisfy a predicate.
///
/// Returns true if the predicate returns non-zero for every element.
/// Returns true for empty sequences (vacuous truth).
///
/// **Example:**
/// ```
/// Function IsPositive(n) As Bool
///     Return Unbox.I64(n) > 0
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// nums.Push(Box.I64(3))
/// Print nums.All(AddressOf IsPositive)  ' True
///
/// nums.Push(Box.I64(-1))
/// Print nums.All(AddressOf IsPositive)  ' False
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 1 (true).
/// @param pred Predicate function. If NULL, returns 1 (true).
///
/// @return 1 if all elements match, 0 otherwise.
///
/// @note O(n) worst case, but short-circuits on first non-match.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_any For checking if any element matches
/// @see rt_seq_none For checking if no elements match
int8_t rt_seq_all(void *obj, int8_t (*pred)(void *))
{
    if (!obj || !pred)
        return 1;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    for (int64_t i = 0; i < seq->len; i++)
    {
        if (!pred(seq->items[i]))
        {
            return 0;
        }
    }

    return 1;
}

/// @brief Check if any element satisfies a predicate.
///
/// Returns true if the predicate returns non-zero for at least one element.
/// Returns false for empty sequences.
///
/// **Example:**
/// ```
/// Function IsNegative(n) As Bool
///     Return Unbox.I64(n) < 0
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// Print nums.Any(AddressOf IsNegative)  ' False
///
/// nums.Push(Box.I64(-1))
/// Print nums.Any(AddressOf IsNegative)  ' True
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 0 (false).
/// @param pred Predicate function. If NULL, returns 0 (false).
///
/// @return 1 if any element matches, 0 otherwise.
///
/// @note O(n) worst case, but short-circuits on first match.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_all For checking if all elements match
/// @see rt_seq_none For checking if no elements match
int8_t rt_seq_any(void *obj, int8_t (*pred)(void *))
{
    if (!obj || !pred)
        return 0;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    for (int64_t i = 0; i < seq->len; i++)
    {
        if (pred(seq->items[i]))
        {
            return 1;
        }
    }

    return 0;
}

/// @brief Check if no elements satisfy a predicate.
///
/// Returns true if the predicate returns zero for every element.
/// Returns true for empty sequences.
///
/// **Example:**
/// ```
/// Function IsNull(obj) As Bool
///     Return obj = Nothing
/// End Function
///
/// Dim items = Seq.New()
/// items.Push("a")
/// items.Push("b")
/// Print items.None(AddressOf IsNull)  ' True (no nulls)
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 1 (true).
/// @param pred Predicate function. If NULL, returns 1 (true).
///
/// @return 1 if no elements match, 0 otherwise.
///
/// @note O(n) worst case, but short-circuits on first match.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_all For checking if all elements match
/// @see rt_seq_any For checking if any element matches
int8_t rt_seq_none(void *obj, int8_t (*pred)(void *))
{
    return !rt_seq_any(obj, pred);
}

/// @brief Count elements that satisfy a predicate.
///
/// **Example:**
/// ```
/// Function StartsWithA(s) As Bool
///     Return Left(s, 1) = "A"
/// End Function
///
/// Dim words = Seq.New()
/// words.Push("Apple")
/// words.Push("Banana")
/// words.Push("Apricot")
/// words.Push("Cherry")
/// Print words.CountWhere(AddressOf StartsWithA)  ' 2
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 0.
/// @param pred Predicate function. If NULL, returns total length.
///
/// @return Number of elements for which predicate returns non-zero.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
int64_t rt_seq_count_where(void *obj, int8_t (*pred)(void *))
{
    if (!obj)
        return 0;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (!pred)
        return seq->len;

    int64_t count = 0;
    for (int64_t i = 0; i < seq->len; i++)
    {
        if (pred(seq->items[i]))
        {
            count++;
        }
    }

    return count;
}

/// @brief Find the first element satisfying a predicate.
///
/// **Example:**
/// ```
/// Function IsLong(s) As Bool
///     Return Len(s) > 5
/// End Function
///
/// Dim words = Seq.New()
/// words.Push("hi")
/// words.Push("hello")
/// words.Push("wonderful")
/// words.Push("world")
/// Dim found = words.FindWhere(AddressOf IsLong)
/// Print found  ' "wonderful"
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns NULL.
/// @param pred Predicate function. If NULL, returns first element or NULL.
///
/// @return First matching element, or NULL if none found.
///
/// @note O(n) worst case, but short-circuits on first match.
/// @note Thread safety: Not thread-safe.
void *rt_seq_find_where(void *obj, int8_t (*pred)(void *))
{
    if (!obj)
        return NULL;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
        return NULL;

    if (!pred)
        return seq->items[0];

    for (int64_t i = 0; i < seq->len; i++)
    {
        if (pred(seq->items[i]))
        {
            return seq->items[i];
        }
    }

    return NULL;
}

/// @brief Create a new Seq with the first N elements.
///
/// **Example:**
/// ```
/// Dim nums = Seq.New()
/// For i = 1 To 10
///     nums.Push(Box.I64(i))
/// Next
/// Dim first3 = nums.Take(3)
/// ' first3 is: [1, 2, 3]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param n Number of elements to take. Clamped to [0, len].
///
/// @return New Seq containing at most n elements from the start.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_drop For skipping elements
/// @see rt_seq_slice For arbitrary ranges
void *rt_seq_take(void *obj, int64_t n)
{
    if (!obj || n <= 0)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (n >= seq->len)
        return rt_seq_clone(obj);

    return rt_seq_slice(obj, 0, n);
}

/// @brief Create a new Seq skipping the first N elements.
///
/// **Example:**
/// ```
/// Dim nums = Seq.New()
/// For i = 1 To 5
///     nums.Push(Box.I64(i))
/// Next
/// Dim rest = nums.Drop(2)
/// ' rest is: [3, 4, 5]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param n Number of elements to skip. Clamped to [0, len].
///
/// @return New Seq containing elements after the first n.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_take For taking elements
/// @see rt_seq_slice For arbitrary ranges
void *rt_seq_drop(void *obj, int64_t n)
{
    if (!obj)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (n <= 0)
        return rt_seq_clone(obj);

    if (n >= seq->len)
        return rt_seq_new();

    return rt_seq_slice(obj, n, seq->len);
}

/// @brief Create a new Seq with elements taken while predicate is true.
///
/// Takes elements from the start while the predicate returns non-zero.
/// Stops at the first element where predicate is false.
///
/// **Example:**
/// ```
/// Function LessThan5(n) As Bool
///     Return Unbox.I64(n) < 5
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(3))
/// nums.Push(Box.I64(7))
/// nums.Push(Box.I64(2))
/// Dim taken = nums.TakeWhile(AddressOf LessThan5)
/// ' taken is: [1, 3] (stops at 7)
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param pred Predicate function. If NULL, returns clone.
///
/// @return New Seq with leading elements matching predicate.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_drop_while For the inverse operation
void *rt_seq_take_while(void *obj, int8_t (*pred)(void *))
{
    if (!obj)
        return rt_seq_new();

    if (!pred)
        return rt_seq_clone(obj);

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *result = rt_seq_new();

    for (int64_t i = 0; i < seq->len; i++)
    {
        if (!pred(seq->items[i]))
        {
            break;
        }
        rt_seq_push(result, seq->items[i]);
    }

    return result;
}

/// @brief Create a new Seq skipping elements while predicate is true.
///
/// Skips elements from the start while the predicate returns non-zero.
/// Includes all elements from the first non-match onwards.
///
/// **Example:**
/// ```
/// Function LessThan5(n) As Bool
///     Return Unbox.I64(n) < 5
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(3))
/// nums.Push(Box.I64(7))
/// nums.Push(Box.I64(2))
/// Dim rest = nums.DropWhile(AddressOf LessThan5)
/// ' rest is: [7, 2] (skipped 1, 3)
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param pred Predicate function. If NULL, returns empty Seq.
///
/// @return New Seq with elements after the leading matching ones.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_take_while For the inverse operation
void *rt_seq_drop_while(void *obj, int8_t (*pred)(void *))
{
    if (!obj)
        return rt_seq_new();

    if (!pred)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    // Find the first non-matching element
    int64_t start = 0;
    while (start < seq->len && pred(seq->items[start]))
    {
        start++;
    }

    return rt_seq_slice(obj, start, seq->len);
}

/// @brief Reduce the sequence to a single value using an accumulator.
///
/// Applies the reducer function to each element and an accumulator,
/// threading the result through as the new accumulator.
///
/// **Example:**
/// ```
/// Function Sum(acc, n) As Object
///     Return Box.I64(Unbox.I64(acc) + Unbox.I64(n))
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// nums.Push(Box.I64(3))
/// nums.Push(Box.I64(4))
/// Dim total = nums.Fold(Box.I64(0), AddressOf Sum)
/// Print Unbox.I64(total)  ' 10
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns init.
/// @param init Initial accumulator value.
/// @param fn Reducer function: fn(accumulator, element) -> new accumulator.
///           If NULL, returns init.
///
/// @return Final accumulated value.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
void *rt_seq_fold(void *obj, void *init, void *(*fn)(void *, void *))
{
    if (!obj || !fn)
        return init;

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *acc = init;

    for (int64_t i = 0; i < seq->len; i++)
    {
        acc = fn(acc, seq->items[i]);
    }

    return acc;
}
