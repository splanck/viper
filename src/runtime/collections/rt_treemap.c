//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_treemap.c
// Purpose: Implements a sorted string-keyed map (TreeMap) backed by a
//   dynamically-resizing sorted array with binary search. Keys are maintained
//   in ascending lexicographic order at all times, supporting ordered iteration
//   and range queries (Floor, Ceiling, First, Last) not available in the
//   unordered Map.
//
// Key invariants:
//   - Entries array is sorted by key in ascending strcmp order at all times.
//   - Binary search provides O(log n) lookup, Floor, and Ceiling queries.
//   - Insertion uses binary search to find the insertion point, then memmove
//     to shift the suffix right: O(n) per insert.
//   - Removal uses binary search to find the entry, then memmove to shift the
//     suffix left: O(n) per remove.
//   - Capacity doubles when the array is full (starting from 8 entries).
//   - Each entry stores a heap-copied key string (owned) and a void* value
//     (not retained). Values must be kept alive by the caller.
//   - Floor(k): largest key <= k; Ceiling(k): smallest key >= k; both O(log n).
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - TreeMap objects are GC-managed (rt_obj_new_i64). The entries array and
//     all heap-copied key strings are freed by the GC finalizer (treemap_finalizer).
//
// Links: src/runtime/collections/rt_treemap.h (public API),
//        src/runtime/collections/rt_map.h (unordered hash map counterpart)
//
//===----------------------------------------------------------------------===//

#include "rt_treemap.h"

#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// @brief Initial capacity for the entries array when first allocation occurs.
///
/// Starting with 8 entries provides a reasonable balance between memory
/// efficiency for small maps and reducing reallocation frequency.
#define TREEMAP_INITIAL_CAPACITY 8

/// @brief A single key-value entry in the TreeMap.
///
/// Each entry owns a copy of the key string and retains a reference to
/// the value. Entries are stored in an array sorted by key to enable
/// binary search lookup.
typedef struct
{
    char *key;     ///< Owned copy of key string (heap-allocated, null-terminated).
    size_t keylen; ///< Length of key in bytes (excluding null terminator).
    void *value;   ///< Retained value pointer (reference count incremented).
} treemap_entry;

/// @brief Internal implementation structure for the TreeMap container.
///
/// TreeMap maintains entries in a dynamically-sized array that is always
/// kept sorted by key. This enables O(log n) lookup via binary search
/// at the cost of O(n) insertion and deletion (due to array shifting).
///
/// **Invariants:**
/// - entries[i].key < entries[i+1].key for all valid i (lexicographic order)
/// - count <= capacity
/// - All keys are non-NULL and null-terminated
/// - All values have their reference counts incremented
typedef struct
{
    void **vptr;            ///< Vtable pointer placeholder (for OOP compatibility).
    treemap_entry *entries; ///< Sorted array of entries (NULL if capacity == 0).
    size_t capacity;        ///< Allocated size of entries array.
    size_t count;           ///< Number of entries currently stored.
} treemap_impl;

/// @brief Extracts raw key data and length from an rt_string.
///
/// Converts a Viper string to a C string pointer and computes its length.
/// Handles NULL strings gracefully by returning an empty string.
///
/// @param key The Viper string to extract data from.
/// @param out_len Output parameter that receives the key length in bytes.
///
/// @return Pointer to the key's character data, or "" if key is NULL.
static const char *get_key_data(rt_string key, size_t *out_len)
{
    const char *cstr = rt_string_cstr(key);
    if (!cstr)
    {
        *out_len = 0;
        return "";
    }
    *out_len = strlen(cstr);
    return cstr;
}

/// @brief Compares two keys lexicographically.
///
/// Performs a byte-by-byte comparison of two keys, returning a value
/// indicating their relative order. This establishes the total ordering
/// used to maintain sorted entries.
///
/// @param k1 First key data.
/// @param len1 Length of first key.
/// @param k2 Second key data.
/// @param len2 Length of second key.
///
/// @return Negative if k1 < k2, zero if k1 == k2, positive if k1 > k2.
static int key_compare(const char *k1, size_t len1, const char *k2, size_t len2)
{
    size_t minlen = len1 < len2 ? len1 : len2;
    int cmp = memcmp(k1, k2, minlen);
    if (cmp != 0)
        return cmp;
    if (len1 < len2)
        return -1;
    if (len1 > len2)
        return 1;
    return 0;
}

/// @brief Searches for a key using binary search.
///
/// Performs binary search on the sorted entries array to find the position
/// of a key. If the key exists, returns its index. If not, returns the
/// index where it should be inserted to maintain sorted order.
///
/// **Algorithm:** Standard binary search with O(log n) comparisons.
///
/// @param tm The TreeMap to search.
/// @param key The key data to search for.
/// @param keylen Length of the key in bytes.
/// @param found Output parameter set to true if exact match found, false otherwise.
///
/// @return The index of the key if found, or the insertion point if not found.
///         The insertion point is the index where the key would be inserted
///         to maintain sorted order.
static size_t binary_search(treemap_impl *tm, const char *key, size_t keylen, bool *found)
{
    *found = false;
    if (tm->count == 0)
        return 0;

    size_t lo = 0;
    size_t hi = tm->count;

    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        treemap_entry *e = &tm->entries[mid];
        int cmp = key_compare(key, keylen, e->key, e->keylen);
        if (cmp < 0)
        {
            hi = mid;
        }
        else if (cmp > 0)
        {
            lo = mid + 1;
        }
        else
        {
            *found = true;
            return mid;
        }
    }
    return lo;
}

/// @brief Ensures the entries array has capacity for at least one more entry.
///
/// If the array is full (count == capacity), doubles the capacity using
/// realloc. For the first allocation (capacity == 0), allocates
/// TREEMAP_INITIAL_CAPACITY entries.
///
/// @param tm The TreeMap to grow if needed.
///
/// @note Traps with "TreeMap: memory allocation failed" if realloc fails.
static void ensure_capacity(treemap_impl *tm)
{
    if (tm->count < tm->capacity)
        return;

    size_t new_cap = tm->capacity == 0 ? TREEMAP_INITIAL_CAPACITY : tm->capacity * 2;
    treemap_entry *new_entries =
        (treemap_entry *)realloc(tm->entries, new_cap * sizeof(treemap_entry));
    if (!new_entries)
    {
        rt_trap("TreeMap: memory allocation failed");
        return;
    }
    tm->entries = new_entries;
    tm->capacity = new_cap;
}

/// @brief Frees an entry's key and releases its value.
///
/// Called when removing an entry from the TreeMap. Frees the key string
/// that was allocated when the entry was created, and releases the
/// reference to the value (potentially freeing it if this was the last
/// reference).
///
/// @param e The entry to clean up.
static void free_entry_contents(treemap_entry *e)
{
    free(e->key);
    if (e->value && rt_obj_release_check0(e->value))
        rt_obj_free(e->value);
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Creates a new empty TreeMap.
///
/// Allocates and initializes an empty sorted map. The entries array is
/// not allocated until the first insertion to save memory for empty maps.
///
/// **Usage example:**
/// ```
/// Dim map = TreeMap.New()
/// map.Set("charlie", obj1)
/// map.Set("alpha", obj2)
/// map.Set("bravo", obj3)
/// ' Keys are stored in sorted order: alpha, bravo, charlie
/// ```
///
/// @return A pointer to the newly created TreeMap.
///
/// @note O(1) time complexity.
/// @note Traps if memory allocation fails.
///
/// @see rt_treemap_set For adding key-value pairs
/// @see rt_treemap_keys For retrieving keys in sorted order
void *rt_treemap_new(void)
{
    treemap_impl *tm = (treemap_impl *)rt_obj_new_i64(0, (int64_t)sizeof(treemap_impl));
    if (!tm)
    {
        rt_trap("TreeMap: memory allocation failed");
        return NULL;
    }

    tm->vptr = NULL;
    tm->entries = NULL;
    tm->capacity = 0;
    tm->count = 0;

    return tm;
}

/// @brief Returns the number of key-value pairs in the TreeMap.
///
/// @param obj Pointer to a TreeMap object.
///
/// @return The number of entries in the map.
///
/// @note O(1) time complexity.
int64_t rt_treemap_len(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;
    return (int64_t)tm->count;
}

/// @brief Checks whether the TreeMap contains no entries.
///
/// @param obj Pointer to a TreeMap object.
///
/// @return 1 (true) if the TreeMap is empty, 0 (false) otherwise.
///
/// @note O(1) time complexity.
int8_t rt_treemap_is_empty(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;
    return tm->count == 0 ? 1 : 0;
}

/// @brief Sets or updates a key-value pair in the TreeMap.
///
/// If the key already exists, updates its value (releasing the old value
/// and retaining the new one). If the key doesn't exist, inserts a new
/// entry at the correct sorted position.
///
/// **Insertion maintains sorted order:**
/// ```
/// Before: [alpha, charlie, delta]
/// Set("bravo", val)
/// After:  [alpha, bravo, charlie, delta]
/// ```
///
/// @param obj Pointer to a TreeMap object.
/// @param key The key string. A copy is made and stored.
/// @param value The value to associate with the key. May be NULL.
///              The TreeMap retains a reference to this value.
///
/// @note O(log n) for lookup + O(n) for insertion (array shifting).
/// @note Traps if memory allocation fails.
///
/// @see rt_treemap_get For retrieving values by key
void rt_treemap_set(void *obj, rt_string key, void *value)
{
    treemap_impl *tm = (treemap_impl *)obj;

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (found)
    {
        // Update existing entry
        treemap_entry *e = &tm->entries[idx];
        // Release old value
        if (e->value && rt_obj_release_check0(e->value))
            rt_obj_free(e->value);
        // Retain new value
        rt_obj_retain_maybe(value);
        e->value = value;
    }
    else
    {
        // Insert new entry
        ensure_capacity(tm);

        // Make room by shifting entries
        if (idx < tm->count)
        {
            memmove(&tm->entries[idx + 1],
                    &tm->entries[idx],
                    (tm->count - idx) * sizeof(treemap_entry));
        }

        // Create new entry
        treemap_entry *e = &tm->entries[idx];
        e->key = (char *)malloc(keylen + 1);
        if (!e->key)
        {
            rt_trap("TreeMap: memory allocation failed");
            return;
        }
        memcpy(e->key, keydata, keylen);
        e->key[keylen] = '\0';
        e->keylen = keylen;

        // Retain value
        rt_obj_retain_maybe(value);
        e->value = value;

        tm->count++;
    }
}

/// @brief Retrieves the value associated with a key.
///
/// Performs binary search to find the key and returns its associated value.
///
/// @param obj Pointer to a TreeMap object.
/// @param key The key to look up.
///
/// @return The value associated with the key, or NULL if the key is not found.
///
/// @note O(log n) time complexity (binary search).
///
/// @see rt_treemap_has For checking if a key exists
/// @see rt_treemap_set For storing key-value pairs
void *rt_treemap_get(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (found)
        return tm->entries[idx].value;
    return NULL;
}

/// @brief Checks whether a key exists in the TreeMap.
///
/// @param obj Pointer to a TreeMap object.
/// @param key The key to check for.
///
/// @return 1 (true) if the key exists, 0 (false) otherwise.
///
/// @note O(log n) time complexity (binary search).
int8_t rt_treemap_has(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    binary_search(tm, keydata, keylen, &found);

    return found ? 1 : 0;
}

/// @brief Removes a key-value pair from the TreeMap.
///
/// If the key exists, removes the entry, frees the key copy, releases
/// the value reference, and shifts remaining entries to maintain sorted order.
///
/// @param obj Pointer to a TreeMap object.
/// @param key The key to remove.
///
/// @return 1 (true) if the key was found and removed, 0 (false) if not found.
///
/// @note O(log n) for lookup + O(n) for removal (array shifting).
int8_t rt_treemap_drop(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (!found)
        return 0;

    // Free the entry
    free_entry_contents(&tm->entries[idx]);

    // Shift remaining entries
    if (idx < tm->count - 1)
    {
        memmove(&tm->entries[idx],
                &tm->entries[idx + 1],
                (tm->count - idx - 1) * sizeof(treemap_entry));
    }

    tm->count--;
    return 1;
}

/// @brief Removes all key-value pairs from the TreeMap.
///
/// Frees all key copies and releases all value references. The entries
/// array capacity remains allocated for potential reuse.
///
/// @param obj Pointer to a TreeMap object.
///
/// @note O(n) time complexity where n is the number of entries.
void rt_treemap_clear(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;

    for (size_t i = 0; i < tm->count; i++)
    {
        free_entry_contents(&tm->entries[i]);
    }

    tm->count = 0;
}

/// @brief Returns all keys in the TreeMap as a Seq, in sorted order.
///
/// Creates a new Seq containing all keys from the TreeMap. Because the
/// TreeMap maintains sorted order internally, the keys in the returned
/// Seq are already in lexicographic order.
///
/// **Usage example:**
/// ```
/// map.Set("charlie", v1)
/// map.Set("alpha", v2)
/// map.Set("bravo", v3)
/// Dim keys = map.Keys()
/// ' keys = ["alpha", "bravo", "charlie"]
/// ```
///
/// @param obj Pointer to a TreeMap object.
///
/// @return A new Seq containing all keys in sorted order.
///
/// @note O(n) time complexity where n is the number of entries.
///
/// @see rt_treemap_values For retrieving values
void *rt_treemap_keys(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;
    void *seq = rt_seq_new();

    for (size_t i = 0; i < tm->count; i++)
    {
        rt_string str = rt_string_from_bytes(tm->entries[i].key, tm->entries[i].keylen);
        rt_seq_push(seq, (void *)str);
    }

    return seq;
}

/// @brief Returns all values in the TreeMap as a Seq, in key-sorted order.
///
/// Creates a new Seq containing all values from the TreeMap. Values appear
/// in the same order as their corresponding keys (sorted lexicographically).
///
/// @param obj Pointer to a TreeMap object.
///
/// @return A new Seq containing all values in key-sorted order.
///
/// @note O(n) time complexity where n is the number of entries.
///
/// @see rt_treemap_keys For retrieving keys
void *rt_treemap_values(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;
    void *seq = rt_seq_new();

    for (size_t i = 0; i < tm->count; i++)
    {
        rt_seq_push(seq, tm->entries[i].value);
    }

    return seq;
}

/// @brief Returns the smallest (first) key in the TreeMap.
///
/// Because keys are stored in sorted order, this returns the lexicographically
/// smallest key, which is the first entry in the sorted array.
///
/// **Usage example:**
/// ```
/// map.Set("charlie", v1)
/// map.Set("alpha", v2)
/// map.Set("bravo", v3)
/// Print map.First()    ' Outputs: "alpha"
/// ```
///
/// @param obj Pointer to a TreeMap object.
///
/// @return The smallest key in the TreeMap, or an empty string if the
///         TreeMap is empty.
///
/// @note O(1) time complexity.
///
/// @see rt_treemap_last For the largest key
rt_string rt_treemap_first(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;

    if (tm->count == 0)
        return rt_const_cstr("");

    return rt_string_from_bytes(tm->entries[0].key, tm->entries[0].keylen);
}

/// @brief Returns the largest (last) key in the TreeMap.
///
/// Because keys are stored in sorted order, this returns the lexicographically
/// largest key, which is the last entry in the sorted array.
///
/// **Usage example:**
/// ```
/// map.Set("charlie", v1)
/// map.Set("alpha", v2)
/// map.Set("bravo", v3)
/// Print map.Last()     ' Outputs: "charlie"
/// ```
///
/// @param obj Pointer to a TreeMap object.
///
/// @return The largest key in the TreeMap, or an empty string if the
///         TreeMap is empty.
///
/// @note O(1) time complexity.
///
/// @see rt_treemap_first For the smallest key
rt_string rt_treemap_last(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;

    if (tm->count == 0)
        return rt_const_cstr("");

    size_t last = tm->count - 1;
    return rt_string_from_bytes(tm->entries[last].key, tm->entries[last].keylen);
}

/// @brief Returns the largest key less than or equal to the given key.
///
/// Performs a "floor" operation: finds the greatest key in the TreeMap
/// that is less than or equal to the specified key. If the key exists,
/// returns it. If not, returns the next smaller key.
///
/// **Usage example:**
/// ```
/// map.Set("apple", v1)
/// map.Set("cherry", v2)
/// map.Set("grape", v3)
///
/// Print map.Floor("cherry")    ' Outputs: "cherry" (exact match)
/// Print map.Floor("date")      ' Outputs: "cherry" (next smaller)
/// Print map.Floor("aardvark")  ' Outputs: "" (nothing smaller)
/// ```
///
/// @param obj Pointer to a TreeMap object.
/// @param key The key to find the floor for.
///
/// @return The floor key, or an empty string if no key <= the given key exists.
///
/// @note O(log n) time complexity (binary search).
///
/// @see rt_treemap_ceil For finding the smallest key >= a given key
rt_string rt_treemap_floor(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    if (tm->count == 0)
        return rt_const_cstr("");

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (found)
    {
        // Exact match
        return rt_string_from_bytes(tm->entries[idx].key, tm->entries[idx].keylen);
    }

    // idx is insertion point - floor is the previous entry
    if (idx == 0)
        return rt_const_cstr(""); // No key <= given key

    return rt_string_from_bytes(tm->entries[idx - 1].key, tm->entries[idx - 1].keylen);
}

/// @brief Returns the smallest key greater than or equal to the given key.
///
/// Performs a "ceiling" operation: finds the smallest key in the TreeMap
/// that is greater than or equal to the specified key. If the key exists,
/// returns it. If not, returns the next larger key.
///
/// **Usage example:**
/// ```
/// map.Set("apple", v1)
/// map.Set("cherry", v2)
/// map.Set("grape", v3)
///
/// Print map.Ceil("cherry")     ' Outputs: "cherry" (exact match)
/// Print map.Ceil("date")       ' Outputs: "grape" (next larger)
/// Print map.Ceil("zebra")      ' Outputs: "" (nothing larger)
/// ```
///
/// @param obj Pointer to a TreeMap object.
/// @param key The key to find the ceiling for.
///
/// @return The ceiling key, or an empty string if no key >= the given key exists.
///
/// @note O(log n) time complexity (binary search).
///
/// @see rt_treemap_floor For finding the largest key <= a given key
rt_string rt_treemap_ceil(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    if (tm->count == 0)
        return rt_const_cstr("");

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (found)
    {
        // Exact match
        return rt_string_from_bytes(tm->entries[idx].key, tm->entries[idx].keylen);
    }

    // idx is insertion point - ceiling is the entry at idx (if exists)
    if (idx >= tm->count)
        return rt_const_cstr(""); // No key >= given key

    return rt_string_from_bytes(tm->entries[idx].key, tm->entries[idx].keylen);
}
