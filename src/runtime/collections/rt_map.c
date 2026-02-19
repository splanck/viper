//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_map.c
// Purpose: Implement a string-keyed hash map using FNV-1a hash with chaining.
// Structure: [vptr | buckets | capacity | count]
// - vptr: points to class vtable (placeholder for OOP compatibility)
// - buckets: array of entry chain heads
// - capacity: number of buckets
// - count: number of entries
//
//===----------------------------------------------------------------------===//

#include "rt_map.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// Initial number of buckets.
#define MAP_INITIAL_CAPACITY 16

/// Load factor threshold for resizing (0.75 = 75%).
#define MAP_LOAD_FACTOR_NUM 3
#define MAP_LOAD_FACTOR_DEN 4

#include "rt_hash_util.h"

/// @brief Entry in the hash map (collision chain node).
///
/// Each entry stores a key-value pair in the Map. Entries are organized into
/// collision chains (linked lists) within each bucket. The Map owns a copy
/// of each string key and retains a reference to each value.
typedef struct rt_map_entry
{
    char *key;                 ///< Owned copy of key string (null-terminated).
    size_t key_len;            ///< Length of key string (excluding null terminator).
    void *value;               ///< Retained reference to the value object.
    struct rt_map_entry *next; ///< Next entry in collision chain (or NULL).
} rt_map_entry;

/// @brief Map (string-to-object dictionary) implementation structure.
///
/// The Map is implemented as a hash table with separate chaining for
/// collision resolution. It provides O(1) average-case lookup, insertion,
/// and deletion for string-keyed associations.
///
/// **Hash table structure:**
/// ```
/// buckets array:
///   [0] -> entry("apple", valA) -> entry("apricot", valB) -> NULL
///   [1] -> NULL
///   [2] -> entry("banana", valC) -> NULL
///   [3] -> entry("cherry", valD) -> entry("coconut", valE) -> NULL
///   ...
///   [capacity-1] -> NULL
/// ```
///
/// **Hash function:**
/// Uses FNV-1a, a fast non-cryptographic hash with good distribution.
///
/// **Load factor:**
/// Resizes when count/capacity exceeds 75% (3/4) to maintain O(1) performance.
///
/// **Key/Value ownership:**
/// - Keys: The Map owns copies of all keys (not references to originals)
/// - Values: The Map retains references (increments ref count)
typedef struct rt_map_impl
{
    void **vptr;            ///< Vtable pointer placeholder (for OOP compatibility).
    rt_map_entry **buckets; ///< Array of bucket heads (collision chain pointers).
    size_t capacity;        ///< Number of buckets in the hash table.
    size_t count;           ///< Number of key-value pairs currently in the Map.
} rt_map_impl;

/// @brief Extracts C string data and length from a Viper string.
///
/// Helper function to safely get the underlying character data from a
/// Viper string object for use with the hash table operations.
///
/// @param key The Viper string to extract data from.
/// @param out_len Pointer to receive the string length.
///
/// @return Pointer to the string's character data (not owned by caller).
///         Returns "" with length 0 if key is NULL.
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

/// @brief Finds an entry in a bucket's collision chain.
///
/// Performs a linear search through the linked list of entries in a bucket
/// to find one matching the given key.
///
/// @param head The head of the collision chain to search.
/// @param key The key string to search for.
/// @param key_len Length of the key string.
///
/// @return Pointer to the matching entry, or NULL if not found.
///
/// @note O(k) time where k is the chain length.
static rt_map_entry *find_entry(rt_map_entry *head, const char *key, size_t key_len)
{
    for (rt_map_entry *e = head; e; e = e->next)
    {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

/// @brief Frees an entry, its owned key, and releases its value reference.
///
/// Releases all resources associated with a map entry:
/// 1. Frees the copied key string
/// 2. Releases the reference to the value (may free if last reference)
/// 3. Frees the entry structure itself
///
/// @param entry The entry to free. If NULL, this is a no-op.
static void free_entry(rt_map_entry *entry)
{
    if (entry)
    {
        free(entry->key);
        if (entry->value && rt_obj_release_check0(entry->value))
            rt_obj_free(entry->value);
        free(entry);
    }
}

/// @brief Finalizer callback invoked when a Map is garbage collected.
///
/// This function is automatically called by Viper's garbage collector when a
/// Map object becomes unreachable. It clears all entries (freeing keys and
/// releasing value references) and frees the buckets array.
///
/// @param obj Pointer to the Map object being finalized. May be NULL (no-op).
///
/// @note This function is idempotent - safe to call on already-finalized maps.
static void rt_map_finalize(void *obj)
{
    if (!obj)
        return;
    rt_map_impl *map = (rt_map_impl *)obj;
    if (!map->buckets || map->capacity == 0)
        return;
    rt_map_clear(map);
    free(map->buckets);
    map->buckets = NULL;
    map->capacity = 0;
    map->count = 0;
}

/// @brief Resizes the hash table to a new capacity and rehashes all entries.
///
/// When the load factor becomes too high, this function creates a new larger
/// bucket array and rehashes all existing entries to maintain O(1) performance.
///
/// @param map The Map to resize.
/// @param new_capacity The new number of buckets.
///
/// @note On allocation failure, the old buckets are kept (silent failure).
/// @note O(n) time complexity where n is the number of entries.
static void map_resize(rt_map_impl *map, size_t new_capacity)
{
    rt_map_entry **new_buckets = (rt_map_entry **)calloc(new_capacity, sizeof(rt_map_entry *));
    if (!new_buckets)
        return; // Keep old buckets on allocation failure

    // Rehash all entries
    for (size_t i = 0; i < map->capacity; ++i)
    {
        rt_map_entry *entry = map->buckets[i];
        while (entry)
        {
            rt_map_entry *next = entry->next;
            uint64_t hash = rt_fnv1a(entry->key, entry->key_len);
            size_t idx = hash % new_capacity;
            entry->next = new_buckets[idx];
            new_buckets[idx] = entry;
            entry = next;
        }
    }

    free(map->buckets);
    map->buckets = new_buckets;
    map->capacity = new_capacity;
}

/// @brief Checks if resize is needed and performs it.
///
/// Triggers a resize when the load factor exceeds 75% (3/4).
///
/// @param map The Map to potentially resize.
///
/// @note The capacity doubles on each resize.
static void maybe_resize(rt_map_impl *map)
{
    // Resize when count * DEN > capacity * NUM (i.e., load factor > NUM/DEN)
    if (map->count * MAP_LOAD_FACTOR_DEN > map->capacity * MAP_LOAD_FACTOR_NUM)
    {
        map_resize(map, map->capacity * 2);
    }
}

/// @brief Creates a new empty Map (string-to-object dictionary).
///
/// Allocates and initializes a Map data structure for storing key-value pairs
/// where keys are strings and values are objects. The Map uses a hash table
/// with separate chaining for O(1) average-case operations.
///
/// **Usage example:**
/// ```
/// Dim map = Map.New()
/// map.Set("name", "Alice")
/// map.Set("age", 30)
/// Print map.Get("name")  ' Outputs: Alice
/// Print map.Len()        ' Outputs: 2
/// ```
///
/// **Implementation notes:**
/// - Uses FNV-1a hash function for key hashing
/// - Collision resolution via separate chaining (linked lists)
/// - Automatically grows when load factor exceeds 75%
///
/// @return A pointer to the newly created Map object, or NULL if memory
///         allocation fails for the Map structure.
///
/// @note Initial capacity is 16 buckets.
/// @note Keys are copied (not referenced), values are retained (ref counted).
/// @note Thread safety: Not thread-safe. External synchronization required.
///
/// @see rt_map_set For adding key-value pairs
/// @see rt_map_get For retrieving values
/// @see rt_map_finalize For cleanup behavior
void *rt_map_new(void)
{
    rt_map_impl *map = (rt_map_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_map_impl));
    if (!map)
        return NULL;

    map->vptr = NULL;
    map->buckets = (rt_map_entry **)calloc(MAP_INITIAL_CAPACITY, sizeof(rt_map_entry *));
    if (!map->buckets)
    {
        // Can't trap here, just return partially initialized
        map->capacity = 0;
        map->count = 0;
        rt_obj_set_finalizer(map, rt_map_finalize);
        return map;
    }
    map->capacity = MAP_INITIAL_CAPACITY;
    map->count = 0;
    rt_obj_set_finalizer(map, rt_map_finalize);
    return map;
}

/// @brief Returns the number of key-value pairs in the Map.
///
/// This function returns how many entries have been added to the Map.
/// The count is maintained internally and returned in O(1) time.
///
/// @param obj Pointer to a Map object. If NULL, returns 0.
///
/// @return The number of entries in the Map (>= 0). Returns 0 if obj is NULL.
///
/// @note O(1) time complexity.
///
/// @see rt_map_is_empty For a boolean check
/// @see rt_map_set For operations that may increase the count
/// @see rt_map_remove For operations that decrease the count
int64_t rt_map_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_map_impl *)obj)->count;
}

/// @brief Checks whether the Map contains no entries.
///
/// A Map is considered empty when its count is 0, which occurs:
/// - Immediately after creation
/// - After all entries have been removed
/// - After calling rt_map_clear
///
/// @param obj Pointer to a Map object. If NULL, returns true (1).
///
/// @return 1 (true) if the Map is empty or obj is NULL, 0 (false) otherwise.
///
/// @note O(1) time complexity.
///
/// @see rt_map_len For the exact count
/// @see rt_map_clear For removing all entries
int8_t rt_map_is_empty(void *obj)
{
    return rt_map_len(obj) == 0;
}

/// @brief Sets a value for a key in the Map.
///
/// Associates the given value with the given key. If the key already exists,
/// its value is replaced (the old value's reference is released). If the key
/// is new, a new entry is created.
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// map.Set("name", "Alice")
/// map.Set("age", 30)
/// map.Set("name", "Bob")    ' Replaces "Alice" with "Bob"
/// Print map.Get("name")     ' Outputs: Bob
/// ```
///
/// **Key handling:**
/// The Map copies the key string - the original can be freed after this call.
///
/// **Value handling:**
/// The Map retains a reference to the value. The old value (if replacing)
/// has its reference released.
///
/// @param obj Pointer to a Map object. If NULL, this is a no-op.
/// @param key The string key to associate with the value.
/// @param value The value to store. Reference is retained by the Map.
///
/// @note O(1) average-case time complexity.
/// @note May trigger a resize if the load factor exceeds 75%.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_map_get For retrieving values
/// @see rt_map_has For checking if a key exists
/// @see rt_map_remove For removing entries
void rt_map_set(void *obj, rt_string key, void *value)
{
    if (!obj)
        return;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return; // Bucket allocation failed

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % map->capacity;

    // Check if key already exists
    rt_map_entry *existing = find_entry(map->buckets[idx], key_data, key_len);
    if (existing)
    {
        // Update existing entry
        void *old_value = existing->value;
        rt_obj_retain_maybe(value);
        existing->value = value;
        if (old_value && rt_obj_release_check0(old_value))
            rt_obj_free(old_value);
        return;
    }

    // Create new entry
    rt_map_entry *entry = (rt_map_entry *)malloc(sizeof(rt_map_entry));
    if (!entry)
        return;

    entry->key = (char *)malloc(key_len + 1);
    if (!entry->key)
    {
        free(entry);
        return;
    }
    memcpy(entry->key, key_data, key_len);
    entry->key[key_len] = '\0';
    entry->key_len = key_len;

    rt_obj_retain_maybe(value);
    entry->value = value;

    // Insert at head of bucket chain
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->count++;

    maybe_resize(map);
}

/// @brief Retrieves the value associated with a key.
///
/// Looks up the key in the Map and returns its associated value. Returns NULL
/// if the key is not found.
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// map.Set("name", "Alice")
/// Print map.Get("name")     ' Outputs: Alice
/// Print map.Get("missing")  ' Outputs: Nothing (NULL)
/// ```
///
/// @param obj Pointer to a Map object. If NULL, returns NULL.
/// @param key The string key to look up.
///
/// @return The value associated with the key, or NULL if not found.
///
/// @note O(1) average-case time complexity.
/// @note Does not modify the Map.
/// @note Thread safety: Safe for concurrent reads if no concurrent writes.
///
/// @see rt_map_get_or For providing a default value
/// @see rt_map_has For checking existence without retrieving
/// @see rt_map_set For storing values
void *rt_map_get(void *obj, rt_string key)
{
    if (!obj)
        return NULL;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return NULL;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % map->capacity;

    rt_map_entry *entry = find_entry(map->buckets[idx], key_data, key_len);
    return entry ? entry->value : NULL;
}

/// @brief Retrieves the value associated with a key, or a default if not found.
///
/// Looks up the key in the Map and returns its associated value. If the key
/// is not found, returns the provided default value instead of NULL.
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// map.Set("name", "Alice")
/// Print map.GetOr("name", "Unknown")    ' Outputs: Alice
/// Print map.GetOr("missing", "Unknown") ' Outputs: Unknown
/// ```
///
/// **Comparison with Get:**
/// - Get returns NULL for missing keys
/// - GetOr returns your chosen default for missing keys
///
/// @param obj Pointer to a Map object. If NULL, returns default_value.
/// @param key The string key to look up.
/// @param default_value The value to return if the key is not found.
///
/// @return The value associated with the key, or default_value if not found.
///
/// @note O(1) average-case time complexity.
/// @note Does not modify the Map - missing keys do not create new entries.
/// @note Thread safety: Safe for concurrent reads if no concurrent writes.
///
/// @see rt_map_get For returning NULL on missing keys
/// @see rt_map_has For checking existence
void *rt_map_get_or(void *obj, rt_string key, void *default_value)
{
    if (!obj)
        return default_value;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return default_value;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % map->capacity;

    rt_map_entry *entry = find_entry(map->buckets[idx], key_data, key_len);
    return entry ? entry->value : default_value;
}

/// @brief Tests whether a key exists in the Map.
///
/// Performs a key lookup without retrieving the value. Useful for checking
/// if a key is present before conditionally operating on it.
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// map.Set("name", "Alice")
/// Print map.Has("name")    ' Outputs: True
/// Print map.Has("missing") ' Outputs: False
/// ```
///
/// @param obj Pointer to a Map object. If NULL, returns 0 (false).
/// @param key The string key to search for.
///
/// @return 1 (true) if the key exists, 0 (false) otherwise.
///
/// @note O(1) average-case time complexity.
/// @note Does not modify the Map.
/// @note Thread safety: Safe for concurrent reads if no concurrent writes.
///
/// @see rt_map_get For retrieving the value
/// @see rt_map_set For adding entries
int8_t rt_map_has(void *obj, rt_string key)
{
    if (!obj)
        return 0;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % map->capacity;

    return find_entry(map->buckets[idx], key_data, key_len) ? 1 : 0;
}

/// @brief Sets a value for a key only if the key doesn't already exist.
///
/// Conditionally inserts a key-value pair. If the key already exists, the Map
/// is not modified and the function returns 0. This is useful for implementing
/// "insert if not exists" logic atomically.
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// Print map.SetIfMissing("name", "Alice")  ' Outputs: 1 (inserted)
/// Print map.SetIfMissing("name", "Bob")    ' Outputs: 0 (already exists)
/// Print map.Get("name")                    ' Outputs: Alice
/// ```
///
/// **Use cases:**
/// - Setting default values only when not already set
/// - First-wins insertion semantics
/// - Implementing caching patterns
///
/// @param obj Pointer to a Map object. If NULL, returns 0.
/// @param key The string key to conditionally set.
/// @param value The value to store if the key is missing.
///
/// @return 1 if the key was missing and the value was inserted, 0 if the key
///         already existed or an error occurred.
///
/// @note O(1) average-case time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_map_set For unconditional set (replaces existing)
/// @see rt_map_has For checking existence
int8_t rt_map_set_if_missing(void *obj, rt_string key, void *value)
{
    if (!obj)
        return 0;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % map->capacity;

    if (find_entry(map->buckets[idx], key_data, key_len))
        return 0;

    rt_map_entry *entry = (rt_map_entry *)malloc(sizeof(rt_map_entry));
    if (!entry)
        return 0;

    entry->key = (char *)malloc(key_len + 1);
    if (!entry->key)
    {
        free(entry);
        return 0;
    }

    memcpy(entry->key, key_data, key_len);
    entry->key[key_len] = '\0';
    entry->key_len = key_len;

    rt_obj_retain_maybe(value);
    entry->value = value;

    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->count++;

    maybe_resize(map);
    return 1;
}

/// @brief Removes the entry with the specified key from the Map.
///
/// Looks up the key and removes its entry if found. The key string is freed
/// and the value's reference is released.
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// map.Set("name", "Alice")
/// map.Set("age", 30)
/// Print map.Remove("name")    ' Outputs: 1 (removed)
/// Print map.Remove("missing") ' Outputs: 0 (not found)
/// Print map.Len()             ' Outputs: 1
/// ```
///
/// @param obj Pointer to a Map object. If NULL, returns 0.
/// @param key The string key to remove.
///
/// @return 1 if the key was found and removed, 0 if not found or obj is NULL.
///
/// @note O(1) average-case time complexity.
/// @note The Map does not shrink after removal - capacity is maintained.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_map_set For adding entries
/// @see rt_map_clear For removing all entries
int8_t rt_map_remove(void *obj, rt_string key)
{
    if (!obj)
        return 0;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % map->capacity;

    rt_map_entry **prev_ptr = &map->buckets[idx];
    rt_map_entry *entry = map->buckets[idx];

    while (entry)
    {
        if (entry->key_len == key_len && memcmp(entry->key, key_data, key_len) == 0)
        {
            *prev_ptr = entry->next;
            free_entry(entry);
            map->count--;
            return 1;
        }
        prev_ptr = &entry->next;
        entry = entry->next;
    }

    return 0;
}

/// @brief Removes all entries from the Map.
///
/// Clears the Map by freeing all entries (keys and releasing value references).
/// After this call, the Map is empty but retains its bucket array capacity
/// for efficient reuse.
///
/// **Memory behavior:**
/// - All entry nodes are freed
/// - All copied key strings are freed
/// - All value references are released
/// - Bucket array is retained (not freed)
/// - Capacity remains unchanged
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// map.Set("a", 1)
/// map.Set("b", 2)
/// Print map.Len()    ' Outputs: 2
/// map.Clear()
/// Print map.Len()    ' Outputs: 0
/// ```
///
/// @param obj Pointer to a Map object. If NULL, this is a no-op.
///
/// @note O(n) time complexity where n is the number of entries.
/// @note The bucket array capacity is preserved for potential reuse.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_map_finalize For complete cleanup including bucket array
/// @see rt_map_is_empty For checking if empty
void rt_map_clear(void *obj)
{
    if (!obj)
        return;

    rt_map_impl *map = (rt_map_impl *)obj;
    for (size_t i = 0; i < map->capacity; ++i)
    {
        rt_map_entry *entry = map->buckets[i];
        while (entry)
        {
            rt_map_entry *next = entry->next;
            free_entry(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->count = 0;
}

/// @brief Returns all keys in the Map as a Seq.
///
/// Creates a new Seq containing copies of all keys currently in the Map.
/// This allows iterating over the Map's keys.
///
/// **Order guarantee:**
/// Keys are returned in hash table iteration order (bucket by bucket, then
/// chain order within each bucket). This order is NOT guaranteed to be
/// consistent across different runs or after modifications to the Map.
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// map.Set("name", "Alice")
/// map.Set("age", 30)
/// Dim keys = map.Keys()
/// For i = 0 To keys.Len() - 1
///     Print keys.Get(i)  ' Outputs each key (order varies)
/// Next
/// ```
///
/// @param obj Pointer to a Map object. If NULL, returns an empty Seq.
///
/// @return A new Seq containing copies of all keys in the Map.
///
/// @note O(n) time and space complexity where n is the number of entries.
/// @note Iteration order is implementation-defined (not sorted).
/// @note Thread safety: Not thread-safe.
///
/// @see rt_map_values For getting all values
void *rt_map_keys(void *obj)
{
    void *result = rt_seq_new();
    if (!obj)
        return result;

    rt_map_impl *map = (rt_map_impl *)obj;

    // Iterate through all buckets and entries
    for (size_t i = 0; i < map->capacity; ++i)
    {
        rt_map_entry *entry = map->buckets[i];
        while (entry)
        {
            // Create a copy of the key as rt_string and push to seq
            rt_string key_str = rt_string_from_bytes(entry->key, entry->key_len);
            rt_seq_push(result, (void *)key_str);
            entry = entry->next;
        }
    }

    return result;
}

/// @brief Returns all values in the Map as a Seq.
///
/// Creates a new Seq containing all values currently in the Map. The values
/// are the same objects as stored in the Map (not copies).
///
/// **Order guarantee:**
/// Values are returned in hash table iteration order (matching the order
/// of Keys()). This order is NOT guaranteed to be consistent.
///
/// **Example:**
/// ```
/// Dim map = Map.New()
/// map.Set("name", "Alice")
/// map.Set("age", 30)
/// Dim values = map.Values()
/// For i = 0 To values.Len() - 1
///     Print values.Get(i)  ' Outputs each value (order varies)
/// Next
/// ```
///
/// @param obj Pointer to a Map object. If NULL, returns an empty Seq.
///
/// @return A new Seq containing all values in the Map.
///
/// @note O(n) time complexity where n is the number of entries.
/// @note Values are the same objects (not copies) - shared with the Map.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_map_keys For getting all keys
void *rt_map_values(void *obj)
{
    void *result = rt_seq_new();
    if (!obj)
        return result;

    rt_map_impl *map = (rt_map_impl *)obj;

    // Iterate through all buckets and entries
    for (size_t i = 0; i < map->capacity; ++i)
    {
        rt_map_entry *entry = map->buckets[i];
        while (entry)
        {
            rt_seq_push(result, entry->value);
            entry = entry->next;
        }
    }

    return result;
}

//=============================================================================
// Typed Accessors (box/unbox wrappers)
//=============================================================================

#include "rt_box.h"

void rt_map_set_int(void *obj, rt_string key, int64_t value)
{
    void *boxed = rt_box_i64(value);
    rt_map_set(obj, key, boxed);
}

int64_t rt_map_get_int(void *obj, rt_string key)
{
    void *val = rt_map_get(obj, key);
    if (!val)
        return 0;
    return rt_unbox_i64(val);
}

int64_t rt_map_get_int_or(void *obj, rt_string key, int64_t def)
{
    void *val = rt_map_get(obj, key);
    if (!val)
        return def;
    return rt_unbox_i64(val);
}

void rt_map_set_float(void *obj, rt_string key, double value)
{
    void *boxed = rt_box_f64(value);
    rt_map_set(obj, key, boxed);
}

double rt_map_get_float(void *obj, rt_string key)
{
    void *val = rt_map_get(obj, key);
    if (!val)
        return 0.0;
    return rt_unbox_f64(val);
}

double rt_map_get_float_or(void *obj, rt_string key, double def)
{
    void *val = rt_map_get(obj, key);
    if (!val)
        return def;
    return rt_unbox_f64(val);
}

void rt_map_set_str(void *obj, rt_string key, rt_string value)
{
    rt_map_set(obj, key, (void *)value);
}

rt_string rt_map_get_str(void *obj, rt_string key)
{
    void *val = rt_map_get(obj, key);
    if (!val)
        return rt_string_from_bytes("", 0);
    return (rt_string)val;
}
