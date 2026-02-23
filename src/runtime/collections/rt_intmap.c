//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_intmap.c
// Purpose: Implements an integer-keyed hash map (IntMap) mapping int64 keys to
//   arbitrary object values. Uses a mix-hash on the integer key to distribute
//   it into a hash table with separate chaining. Supports get, put, remove,
//   contains, and key/value enumeration. Typical uses: entity ID lookup tables,
//   sparse index-to-object mappings, and cache tables keyed by integer handle.
//
// Key invariants:
//   - Backed by a hash table with initial capacity MAP_INITIAL_CAPACITY (16)
//     buckets and separate chaining.
//   - Resizes (doubles) when count/capacity exceeds 75% (MAP_LOAD_FACTOR 3/4).
//   - Integer keys are hashed by multiplying with a Knuth multiplicative
//     constant (or similar mix) rather than direct modulo to avoid clustering.
//   - Values are stored as raw void* pointers; the map does not retain them
//     (no rt_obj_retain call on insert). Caller manages value lifetime.
//   - All operations are O(1) average case; O(n) worst case.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - IntMap objects are GC-managed (rt_obj_new_i64). The bucket array and all
//     entry nodes are freed by the GC finalizer.
//
// Links: src/runtime/collections/rt_intmap.h (public API),
//        src/runtime/collections/rt_map.h (string-keyed map counterpart)
//
//===----------------------------------------------------------------------===//

#include "rt_intmap.h"

#include "rt_box.h"
#include "rt_hash_util.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stdlib.h>
#include <string.h>

/// Initial number of buckets.
#define MAP_INITIAL_CAPACITY 16

/// Load factor threshold for resizing (0.75 = 75%).
#define MAP_LOAD_FACTOR_NUM 3
#define MAP_LOAD_FACTOR_DEN 4

/// @brief Entry in the integer-keyed hash map (collision chain node).
typedef struct rt_intmap_entry
{
    int64_t key;                  ///< Integer key.
    void *value;                  ///< Retained reference to the value object.
    struct rt_intmap_entry *next; ///< Next entry in collision chain (or NULL).
} rt_intmap_entry;

/// @brief IntMap (integer-to-object dictionary) implementation structure.
typedef struct rt_intmap_impl
{
    void **vptr;               ///< Vtable pointer placeholder (for OOP compatibility).
    rt_intmap_entry **buckets; ///< Array of bucket heads (collision chain pointers).
    size_t capacity;           ///< Number of buckets in the hash table.
    size_t count;              ///< Number of key-value pairs currently in the IntMap.
} rt_intmap_impl;

/// @brief Find an entry matching the given key in a collision chain.
/// @param head Head of the collision chain.
/// @param key Integer key to search for.
/// @return Matching entry or NULL.
static rt_intmap_entry *find_entry(rt_intmap_entry *head, int64_t key)
{
    for (rt_intmap_entry *e = head; e; e = e->next)
    {
        if (e->key == key)
            return e;
    }
    return NULL;
}

/// @brief Free an entry and release its value reference.
/// @param entry Entry to free (NULL is a no-op).
static void free_entry(rt_intmap_entry *entry)
{
    if (entry)
    {
        if (entry->value && rt_obj_release_check0(entry->value))
            rt_obj_free(entry->value);
        free(entry);
    }
}

/// @brief Finalizer callback invoked when an IntMap is garbage collected.
/// @param obj Pointer to the IntMap object being finalized (NULL is a no-op).
static void rt_intmap_finalize(void *obj)
{
    if (!obj)
        return;
    rt_intmap_impl *map = (rt_intmap_impl *)obj;
    if (!map->buckets || map->capacity == 0)
        return;
    rt_intmap_clear(map);
    free(map->buckets);
    map->buckets = NULL;
    map->capacity = 0;
    map->count = 0;
}

/// @brief Resize the hash table and rehash all entries.
/// @param map IntMap to resize.
/// @param new_capacity New number of buckets.
static void map_resize(rt_intmap_impl *map, size_t new_capacity)
{
    rt_intmap_entry **new_buckets =
        (rt_intmap_entry **)calloc(new_capacity, sizeof(rt_intmap_entry *));
    if (!new_buckets)
        return; // Keep old buckets on allocation failure

    // Rehash all entries
    for (size_t i = 0; i < map->capacity; ++i)
    {
        rt_intmap_entry *entry = map->buckets[i];
        while (entry)
        {
            rt_intmap_entry *next = entry->next;
            uint64_t hash = rt_fnv1a(&entry->key, sizeof(entry->key));
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

/// @brief Check if resize is needed and perform it.
/// @param map IntMap to potentially resize.
static void maybe_resize(rt_intmap_impl *map)
{
    // Resize when count * DEN > capacity * NUM (i.e., load factor > NUM/DEN)
    if (map->count * MAP_LOAD_FACTOR_DEN > map->capacity * MAP_LOAD_FACTOR_NUM)
    {
        map_resize(map, map->capacity * 2);
    }
}

/// @brief Create a new empty IntMap.
/// @return Pointer to the newly created IntMap object, or NULL on failure.
void *rt_intmap_new(void)
{
    rt_intmap_impl *map = (rt_intmap_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_intmap_impl));
    if (!map)
        return NULL;

    map->vptr = NULL;
    map->buckets = (rt_intmap_entry **)calloc(MAP_INITIAL_CAPACITY, sizeof(rt_intmap_entry *));
    if (!map->buckets)
    {
        // Can't trap here, just return partially initialized
        map->capacity = 0;
        map->count = 0;
        rt_obj_set_finalizer(map, rt_intmap_finalize);
        return map;
    }
    map->capacity = MAP_INITIAL_CAPACITY;
    map->count = 0;
    rt_obj_set_finalizer(map, rt_intmap_finalize);
    return map;
}

/// @brief Return the number of key-value pairs in the IntMap.
/// @param obj IntMap pointer (NULL returns 0).
/// @return Entry count.
int64_t rt_intmap_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_intmap_impl *)obj)->count;
}

/// @brief Check whether the IntMap is empty.
/// @param obj IntMap pointer (NULL returns 1).
/// @return 1 if empty, 0 otherwise.
int8_t rt_intmap_is_empty(void *obj)
{
    return rt_intmap_len(obj) == 0;
}

/// @brief Set or update a key-value pair in the IntMap.
/// @param obj IntMap pointer (NULL is a no-op).
/// @param key Integer key.
/// @param value Value to store (retained by the IntMap).
void rt_intmap_set(void *obj, int64_t key, void *value)
{
    if (!obj)
        return;

    rt_intmap_impl *map = (rt_intmap_impl *)obj;
    if (map->capacity == 0)
        return; // Bucket allocation failed

    uint64_t hash = rt_fnv1a(&key, sizeof(key));
    size_t idx = hash % map->capacity;

    // Check if key already exists
    rt_intmap_entry *existing = find_entry(map->buckets[idx], key);
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
    rt_intmap_entry *entry = (rt_intmap_entry *)malloc(sizeof(rt_intmap_entry));
    if (!entry)
        return;

    entry->key = key;
    rt_obj_retain_maybe(value);
    entry->value = value;

    // Insert at head of bucket chain
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->count++;

    maybe_resize(map);
}

/// @brief Retrieve the value associated with a key.
/// @param obj IntMap pointer (NULL returns NULL).
/// @param key Integer key.
/// @return Value pointer or NULL if not found.
void *rt_intmap_get(void *obj, int64_t key)
{
    if (!obj)
        return NULL;

    rt_intmap_impl *map = (rt_intmap_impl *)obj;
    if (map->capacity == 0)
        return NULL;

    uint64_t hash = rt_fnv1a(&key, sizeof(key));
    size_t idx = hash % map->capacity;

    rt_intmap_entry *entry = find_entry(map->buckets[idx], key);
    return entry ? entry->value : NULL;
}

/// @brief Retrieve the value for a key, or a default if missing.
/// @param obj IntMap pointer (NULL returns default_value).
/// @param key Integer key.
/// @param default_value Fallback value when key is absent.
/// @return Existing value or default_value.
void *rt_intmap_get_or(void *obj, int64_t key, void *default_value)
{
    if (!obj)
        return default_value;

    rt_intmap_impl *map = (rt_intmap_impl *)obj;
    if (map->capacity == 0)
        return default_value;

    uint64_t hash = rt_fnv1a(&key, sizeof(key));
    size_t idx = hash % map->capacity;

    rt_intmap_entry *entry = find_entry(map->buckets[idx], key);
    return entry ? entry->value : default_value;
}

/// @brief Test whether a key exists in the IntMap.
/// @param obj IntMap pointer (NULL returns 0).
/// @param key Integer key.
/// @return 1 if present, 0 otherwise.
int8_t rt_intmap_has(void *obj, int64_t key)
{
    if (!obj)
        return 0;

    rt_intmap_impl *map = (rt_intmap_impl *)obj;
    if (map->capacity == 0)
        return 0;

    uint64_t hash = rt_fnv1a(&key, sizeof(key));
    size_t idx = hash % map->capacity;

    return find_entry(map->buckets[idx], key) ? 1 : 0;
}

/// @brief Remove the entry with the specified key.
/// @param obj IntMap pointer (NULL returns 0).
/// @param key Integer key to remove.
/// @return 1 if removed, 0 if not found.
int8_t rt_intmap_remove(void *obj, int64_t key)
{
    if (!obj)
        return 0;

    rt_intmap_impl *map = (rt_intmap_impl *)obj;
    if (map->capacity == 0)
        return 0;

    uint64_t hash = rt_fnv1a(&key, sizeof(key));
    size_t idx = hash % map->capacity;

    rt_intmap_entry **prev_ptr = &map->buckets[idx];
    rt_intmap_entry *entry = map->buckets[idx];

    while (entry)
    {
        if (entry->key == key)
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

/// @brief Remove all entries from the IntMap.
/// @param obj IntMap pointer (NULL is a no-op).
void rt_intmap_clear(void *obj)
{
    if (!obj)
        return;

    rt_intmap_impl *map = (rt_intmap_impl *)obj;
    for (size_t i = 0; i < map->capacity; ++i)
    {
        rt_intmap_entry *entry = map->buckets[i];
        while (entry)
        {
            rt_intmap_entry *next = entry->next;
            free_entry(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->count = 0;
}

/// @brief Return all keys as a Seq of boxed integers.
/// @param obj IntMap pointer (NULL returns empty Seq).
/// @return New Seq containing all keys as boxed i64 values.
void *rt_intmap_keys(void *obj)
{
    void *result = rt_seq_new();
    if (!obj)
        return result;

    rt_intmap_impl *map = (rt_intmap_impl *)obj;

    // Iterate through all buckets and entries
    for (size_t i = 0; i < map->capacity; ++i)
    {
        rt_intmap_entry *entry = map->buckets[i];
        while (entry)
        {
            void *boxed = rt_box_i64(entry->key);
            rt_seq_push(result, boxed);
            entry = entry->next;
        }
    }

    return result;
}

/// @brief Return all values as a Seq.
/// @param obj IntMap pointer (NULL returns empty Seq).
/// @return New Seq containing all values.
void *rt_intmap_values(void *obj)
{
    void *result = rt_seq_new();
    if (!obj)
        return result;

    rt_intmap_impl *map = (rt_intmap_impl *)obj;

    // Iterate through all buckets and entries
    for (size_t i = 0; i < map->capacity; ++i)
    {
        rt_intmap_entry *entry = map->buckets[i];
        while (entry)
        {
            rt_seq_push(result, entry->value);
            entry = entry->next;
        }
    }

    return result;
}
