//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_dictionary.c
// Purpose: Implement a string-keyed hash map using FNV-1a hash with chaining.
// Structure: [vptr | buckets | capacity | count]
// - vptr: points to class vtable (placeholder for OOP compatibility)
// - buckets: array of entry chain heads
// - capacity: number of buckets
// - count: number of entries
//
//===----------------------------------------------------------------------===//

#include "rt_dictionary.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// Initial number of buckets.
#define DICT_INITIAL_CAPACITY 16

/// Load factor threshold for resizing (0.75 = 75%).
#define DICT_LOAD_FACTOR_NUM 3
#define DICT_LOAD_FACTOR_DEN 4

/// FNV-1a hash constants for 64-bit.
#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

/// @brief Entry in the hash map (collision chain node).
typedef struct rt_dict_entry
{
    char *key;                  ///< Owned copy of key string.
    size_t key_len;             ///< Length of key.
    void *value;                ///< Retained value pointer.
    struct rt_dict_entry *next; ///< Next entry in collision chain.
} rt_dict_entry;

/// @brief Dictionary implementation structure.
typedef struct rt_dict_impl
{
    void **vptr;             ///< Vtable pointer placeholder.
    rt_dict_entry **buckets; ///< Array of bucket heads.
    size_t capacity;         ///< Number of buckets.
    size_t count;            ///< Number of entries.
} rt_dict_impl;

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
static rt_dict_entry *find_entry(rt_dict_entry *head, const char *key, size_t key_len)
{
    for (rt_dict_entry *e = head; e; e = e->next)
    {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

/// @brief Free an entry and its owned key.
static void free_entry(rt_dict_entry *entry)
{
    if (entry)
    {
        free(entry->key);
        if (entry->value && rt_obj_release_check0(entry->value))
            free(entry->value);
        free(entry);
    }
}

/// @brief Resize the hash table.
static void dict_resize(rt_dict_impl *dict, size_t new_capacity)
{
    rt_dict_entry **new_buckets = (rt_dict_entry **)calloc(new_capacity, sizeof(rt_dict_entry *));
    if (!new_buckets)
        return; // Keep old buckets on allocation failure

    // Rehash all entries
    for (size_t i = 0; i < dict->capacity; ++i)
    {
        rt_dict_entry *entry = dict->buckets[i];
        while (entry)
        {
            rt_dict_entry *next = entry->next;
            uint64_t hash = fnv1a_hash(entry->key, entry->key_len);
            size_t idx = hash % new_capacity;
            entry->next = new_buckets[idx];
            new_buckets[idx] = entry;
            entry = next;
        }
    }

    free(dict->buckets);
    dict->buckets = new_buckets;
    dict->capacity = new_capacity;
}

/// @brief Check if resize is needed and perform it.
static void maybe_resize(rt_dict_impl *dict)
{
    // Resize when count * DEN > capacity * NUM (i.e., load factor > NUM/DEN)
    if (dict->count * DICT_LOAD_FACTOR_DEN > dict->capacity * DICT_LOAD_FACTOR_NUM)
    {
        dict_resize(dict, dict->capacity * 2);
    }
}

void *rt_dict_new(void)
{
    rt_dict_impl *dict = (rt_dict_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_dict_impl));
    if (!dict)
        return NULL;

    dict->vptr = NULL;
    dict->buckets = (rt_dict_entry **)calloc(DICT_INITIAL_CAPACITY, sizeof(rt_dict_entry *));
    if (!dict->buckets)
    {
        // Can't trap here, just return partially initialized
        dict->capacity = 0;
        dict->count = 0;
        return dict;
    }
    dict->capacity = DICT_INITIAL_CAPACITY;
    dict->count = 0;
    return dict;
}

void rt_dict_clear(void *dict)
{
    if (!dict)
        return;

    rt_dict_impl *d = (rt_dict_impl *)dict;
    for (size_t i = 0; i < d->capacity; ++i)
    {
        rt_dict_entry *entry = d->buckets[i];
        while (entry)
        {
            rt_dict_entry *next = entry->next;
            free_entry(entry);
            entry = next;
        }
        d->buckets[i] = NULL;
    }
    d->count = 0;
}

int64_t rt_dict_count(void *dict)
{
    if (!dict)
        return 0;
    return (int64_t)((rt_dict_impl *)dict)->count;
}

void rt_dict_set(void *dict, rt_string key, void *value)
{
    if (!dict)
        return;

    rt_dict_impl *d = (rt_dict_impl *)dict;
    if (d->capacity == 0)
        return; // Bucket allocation failed

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % d->capacity;

    // Check if key already exists
    rt_dict_entry *existing = find_entry(d->buckets[idx], key_data, key_len);
    if (existing)
    {
        // Update existing entry
        void *old_value = existing->value;
        rt_obj_retain_maybe(value);
        existing->value = value;
        if (old_value && rt_obj_release_check0(old_value))
            free(old_value);
        return;
    }

    // Create new entry
    rt_dict_entry *entry = (rt_dict_entry *)malloc(sizeof(rt_dict_entry));
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
    entry->next = d->buckets[idx];
    d->buckets[idx] = entry;
    d->count++;

    maybe_resize(d);
}

void *rt_dict_get(void *dict, rt_string key)
{
    if (!dict)
        return NULL;

    rt_dict_impl *d = (rt_dict_impl *)dict;
    if (d->capacity == 0)
        return NULL;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % d->capacity;

    rt_dict_entry *entry = find_entry(d->buckets[idx], key_data, key_len);
    return entry ? entry->value : NULL;
}

int64_t rt_dict_contains_key(void *dict, rt_string key)
{
    if (!dict)
        return 0;

    rt_dict_impl *d = (rt_dict_impl *)dict;
    if (d->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % d->capacity;

    return find_entry(d->buckets[idx], key_data, key_len) ? 1 : 0;
}

int64_t rt_dict_remove(void *dict, rt_string key)
{
    if (!dict)
        return 0;

    rt_dict_impl *d = (rt_dict_impl *)dict;
    if (d->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % d->capacity;

    rt_dict_entry **prev_ptr = &d->buckets[idx];
    rt_dict_entry *entry = d->buckets[idx];

    while (entry)
    {
        if (entry->key_len == key_len && memcmp(entry->key, key_data, key_len) == 0)
        {
            *prev_ptr = entry->next;
            free_entry(entry);
            d->count--;
            return 1;
        }
        prev_ptr = &entry->next;
        entry = entry->next;
    }

    return 0;
}
