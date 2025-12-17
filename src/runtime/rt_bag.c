//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_bag.c
// Purpose: Implement a string set (Bag) using FNV-1a hash with chaining.
// Structure: [vptr | buckets | capacity | count]
// - vptr: points to class vtable (placeholder for OOP compatibility)
// - buckets: array of entry chain heads
// - capacity: number of buckets
// - count: number of entries
//
//===----------------------------------------------------------------------===//

#include "rt_bag.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// Initial number of buckets.
#define BAG_INITIAL_CAPACITY 16

/// Load factor threshold for resizing (0.75 = 75%).
#define BAG_LOAD_FACTOR_NUM 3
#define BAG_LOAD_FACTOR_DEN 4

/// FNV-1a hash constants for 64-bit.
#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

/// @brief Entry in the hash set (collision chain node).
typedef struct rt_bag_entry
{
    char *key;                 ///< Owned copy of string.
    size_t key_len;            ///< Length of string.
    struct rt_bag_entry *next; ///< Next entry in collision chain.
} rt_bag_entry;

/// @brief Bag implementation structure.
typedef struct rt_bag_impl
{
    void **vptr;            ///< Vtable pointer placeholder.
    rt_bag_entry **buckets; ///< Array of bucket heads.
    size_t capacity;        ///< Number of buckets.
    size_t count;           ///< Number of entries.
} rt_bag_impl;

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
static rt_bag_entry *find_entry(rt_bag_entry *head, const char *key, size_t key_len)
{
    for (rt_bag_entry *e = head; e; e = e->next)
    {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

/// @brief Free an entry and its owned key.
static void free_entry(rt_bag_entry *entry)
{
    if (entry)
    {
        free(entry->key);
        free(entry);
    }
}

static void rt_bag_finalize(void *obj)
{
    if (!obj)
        return;
    rt_bag_impl *bag = (rt_bag_impl *)obj;
    if (!bag->buckets || bag->capacity == 0)
        return;
    rt_bag_clear(bag);
    free(bag->buckets);
    bag->buckets = NULL;
    bag->capacity = 0;
    bag->count = 0;
}

/// @brief Resize the hash table.
static void bag_resize(rt_bag_impl *bag, size_t new_capacity)
{
    rt_bag_entry **new_buckets = (rt_bag_entry **)calloc(new_capacity, sizeof(rt_bag_entry *));
    if (!new_buckets)
        return; // Keep old buckets on allocation failure

    // Rehash all entries
    for (size_t i = 0; i < bag->capacity; ++i)
    {
        rt_bag_entry *entry = bag->buckets[i];
        while (entry)
        {
            rt_bag_entry *next = entry->next;
            uint64_t hash = fnv1a_hash(entry->key, entry->key_len);
            size_t idx = hash % new_capacity;
            entry->next = new_buckets[idx];
            new_buckets[idx] = entry;
            entry = next;
        }
    }

    free(bag->buckets);
    bag->buckets = new_buckets;
    bag->capacity = new_capacity;
}

/// @brief Check if resize is needed and perform it.
static void maybe_resize(rt_bag_impl *bag)
{
    // Resize when count * DEN > capacity * NUM (i.e., load factor > NUM/DEN)
    if (bag->count * BAG_LOAD_FACTOR_DEN > bag->capacity * BAG_LOAD_FACTOR_NUM)
    {
        bag_resize(bag, bag->capacity * 2);
    }
}

void *rt_bag_new(void)
{
    rt_bag_impl *bag = (rt_bag_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_bag_impl));
    if (!bag)
        return NULL;

    bag->vptr = NULL;
    bag->buckets = (rt_bag_entry **)calloc(BAG_INITIAL_CAPACITY, sizeof(rt_bag_entry *));
    if (!bag->buckets)
    {
        // Can't trap here, just return partially initialized
        bag->capacity = 0;
        bag->count = 0;
        rt_obj_set_finalizer(bag, rt_bag_finalize);
        return bag;
    }
    bag->capacity = BAG_INITIAL_CAPACITY;
    bag->count = 0;
    rt_obj_set_finalizer(bag, rt_bag_finalize);
    return bag;
}

int64_t rt_bag_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_bag_impl *)obj)->count;
}

int8_t rt_bag_is_empty(void *obj)
{
    return rt_bag_len(obj) == 0;
}

int8_t rt_bag_put(void *obj, rt_string str)
{
    if (!obj)
        return 0;

    rt_bag_impl *bag = (rt_bag_impl *)obj;
    if (bag->capacity == 0)
        return 0; // Bucket allocation failed

    size_t key_len;
    const char *key_data = get_key_data(str, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % bag->capacity;

    // Check if string already exists
    if (find_entry(bag->buckets[idx], key_data, key_len))
    {
        return 0; // Already present
    }

    // Create new entry
    rt_bag_entry *entry = (rt_bag_entry *)malloc(sizeof(rt_bag_entry));
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

    // Insert at head of bucket chain
    entry->next = bag->buckets[idx];
    bag->buckets[idx] = entry;
    bag->count++;

    maybe_resize(bag);
    return 1;
}

int8_t rt_bag_drop(void *obj, rt_string str)
{
    if (!obj)
        return 0;

    rt_bag_impl *bag = (rt_bag_impl *)obj;
    if (bag->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(str, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % bag->capacity;

    rt_bag_entry **prev_ptr = &bag->buckets[idx];
    rt_bag_entry *entry = bag->buckets[idx];

    while (entry)
    {
        if (entry->key_len == key_len && memcmp(entry->key, key_data, key_len) == 0)
        {
            *prev_ptr = entry->next;
            free_entry(entry);
            bag->count--;
            return 1;
        }
        prev_ptr = &entry->next;
        entry = entry->next;
    }

    return 0;
}

int8_t rt_bag_has(void *obj, rt_string str)
{
    if (!obj)
        return 0;

    rt_bag_impl *bag = (rt_bag_impl *)obj;
    if (bag->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(str, &key_len);
    uint64_t hash = fnv1a_hash(key_data, key_len);
    size_t idx = hash % bag->capacity;

    return find_entry(bag->buckets[idx], key_data, key_len) ? 1 : 0;
}

void rt_bag_clear(void *obj)
{
    if (!obj)
        return;

    rt_bag_impl *bag = (rt_bag_impl *)obj;
    for (size_t i = 0; i < bag->capacity; ++i)
    {
        rt_bag_entry *entry = bag->buckets[i];
        while (entry)
        {
            rt_bag_entry *next = entry->next;
            free_entry(entry);
            entry = next;
        }
        bag->buckets[i] = NULL;
    }
    bag->count = 0;
}

void *rt_bag_items(void *obj)
{
    void *result = rt_seq_new();
    if (!obj)
        return result;

    rt_bag_impl *bag = (rt_bag_impl *)obj;

    // Iterate through all buckets and entries
    for (size_t i = 0; i < bag->capacity; ++i)
    {
        rt_bag_entry *entry = bag->buckets[i];
        while (entry)
        {
            // Create a copy of the string and push to seq
            rt_string str = rt_string_from_bytes(entry->key, entry->key_len);
            rt_seq_push(result, (void *)str);
            entry = entry->next;
        }
    }

    return result;
}

void *rt_bag_merge(void *obj, void *other)
{
    void *result = rt_bag_new();
    if (!result)
        return result;

    // Add all elements from first bag
    if (obj)
    {
        rt_bag_impl *bag = (rt_bag_impl *)obj;
        for (size_t i = 0; i < bag->capacity; ++i)
        {
            rt_bag_entry *entry = bag->buckets[i];
            while (entry)
            {
                rt_string str = rt_string_from_bytes(entry->key, entry->key_len);
                rt_bag_put(result, str);
                entry = entry->next;
            }
        }
    }

    // Add all elements from second bag
    if (other)
    {
        rt_bag_impl *bag = (rt_bag_impl *)other;
        for (size_t i = 0; i < bag->capacity; ++i)
        {
            rt_bag_entry *entry = bag->buckets[i];
            while (entry)
            {
                rt_string str = rt_string_from_bytes(entry->key, entry->key_len);
                rt_bag_put(result, str);
                entry = entry->next;
            }
        }
    }

    return result;
}

void *rt_bag_common(void *obj, void *other)
{
    void *result = rt_bag_new();
    if (!result || !obj || !other)
        return result;

    rt_bag_impl *bag = (rt_bag_impl *)obj;

    // For each element in first bag, check if it's in second
    for (size_t i = 0; i < bag->capacity; ++i)
    {
        rt_bag_entry *entry = bag->buckets[i];
        while (entry)
        {
            rt_string str = rt_string_from_bytes(entry->key, entry->key_len);
            if (rt_bag_has(other, str))
            {
                rt_bag_put(result, str);
            }
            entry = entry->next;
        }
    }

    return result;
}

void *rt_bag_diff(void *obj, void *other)
{
    void *result = rt_bag_new();
    if (!result || !obj)
        return result;

    rt_bag_impl *bag = (rt_bag_impl *)obj;

    // For each element in first bag, check if it's NOT in second
    for (size_t i = 0; i < bag->capacity; ++i)
    {
        rt_bag_entry *entry = bag->buckets[i];
        while (entry)
        {
            rt_string str = rt_string_from_bytes(entry->key, entry->key_len);
            if (!other || !rt_bag_has(other, str))
            {
                rt_bag_put(result, str);
            }
            entry = entry->next;
        }
    }

    return result;
}
