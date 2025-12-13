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

/// FNV-1a hash constants for 64-bit.
#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

/// @brief Entry in the hash map (collision chain node).
typedef struct rt_map_entry
{
    char *key;                 ///< Owned copy of key string.
    size_t key_len;            ///< Length of key.
    void *value;               ///< Retained value pointer.
    struct rt_map_entry *next; ///< Next entry in collision chain.
} rt_map_entry;

/// @brief Map implementation structure.
typedef struct rt_map_impl
{
    void **vptr;            ///< Vtable pointer placeholder.
    rt_map_entry **buckets; ///< Array of bucket heads.
    size_t capacity;        ///< Number of buckets.
    size_t count;           ///< Number of entries.
} rt_map_impl;

/// @brief Compute FNV-1a hash of a byte sequence.
static uint64_t fnv1a_hash(const char *data, size_t len)
{
    uint64_t hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= (uint8_t)data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

/// @brief Get key data and length from rt_string.
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

/// @brief Find entry in bucket chain.
static rt_map_entry *find_entry(rt_map_entry *head, const char *key, size_t key_len)
{
    for (rt_map_entry *e = head; e; e = e->next)
    {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

/// @brief Free an entry and its owned key.
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

/// @brief Resize the hash table.
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
            uint64_t hash = fnv1a_hash(entry->key, entry->key_len);
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
static void maybe_resize(rt_map_impl *map)
{
    // Resize when count * DEN > capacity * NUM (i.e., load factor > NUM/DEN)
    if (map->count * MAP_LOAD_FACTOR_DEN > map->capacity * MAP_LOAD_FACTOR_NUM)
    {
        map_resize(map, map->capacity * 2);
    }
}

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
        return map;
    }
    map->capacity = MAP_INITIAL_CAPACITY;
    map->count = 0;
    return map;
}

int64_t rt_map_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_map_impl *)obj)->count;
}

int8_t rt_map_is_empty(void *obj)
{
    return rt_map_len(obj) == 0;
}

void rt_map_set(void *obj, rt_string key, void *value)
{
    if (!obj)
        return;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return; // Bucket allocation failed

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
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

void *rt_map_get(void *obj, rt_string key)
{
    if (!obj)
        return NULL;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return NULL;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % map->capacity;

    rt_map_entry *entry = find_entry(map->buckets[idx], key_data, key_len);
    return entry ? entry->value : NULL;
}

/// @brief Get value for @p key or return @p default_value when missing.
/// @details This helper does not mutate the map: missing keys do not create new entries.
void *rt_map_get_or(void *obj, rt_string key, void *default_value)
{
    if (!obj)
        return default_value;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return default_value;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % map->capacity;

    rt_map_entry *entry = find_entry(map->buckets[idx], key_data, key_len);
    return entry ? entry->value : default_value;
}

int8_t rt_map_has(void *obj, rt_string key)
{
    if (!obj)
        return 0;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % map->capacity;

    return find_entry(map->buckets[idx], key_data, key_len) ? 1 : 0;
}

/// @brief Insert (@p key, @p value) only when @p key is missing.
/// @details Returns 1 when insertion occurs; returns 0 when the key already exists or on failure.
int8_t rt_map_set_if_missing(void *obj, rt_string key, void *value)
{
    if (!obj)
        return 0;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
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

int8_t rt_map_remove(void *obj, rt_string key)
{
    if (!obj)
        return 0;

    rt_map_impl *map = (rt_map_impl *)obj;
    if (map->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
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
