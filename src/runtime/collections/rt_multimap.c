//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_multimap.c
// Purpose: Implements a string-keyed multimap where each key can map to
//   multiple values. Internally backed by a hash table where each bucket entry
//   holds a Seq of values for that key. Supports add (appending to a key's
//   list), get (returning the Seq for a key), remove (removing one value from
//   a key), and removeAll (removing an entire key and its Seq).
//
// Key invariants:
//   - Backed by a hash table with initial capacity MM_INITIAL_CAPACITY (16)
//     buckets and separate chaining using FNV-1a hashing.
//   - Resizes (doubles) when key_count/capacity exceeds 75%.
//   - `key_count` tracks distinct keys; `total_count` tracks total values added.
//   - Each bucket entry stores a Seq (rt_seq); adding a value appends to that
//     Seq. Removing the last value for a key removes the entry entirely.
//   - Getting a non-existent key returns an empty Seq (not NULL) so callers can
//     always iterate the result without a null-check.
//   - All operations are O(1) average case for key lookup; O(k) for removing
//     a specific value from a key with k values.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - MultiMap objects are GC-managed (rt_obj_new_i64). The bucket array, entry
//     nodes, and per-key Seq objects are freed by the GC finalizer.
//
// Links: src/runtime/collections/rt_multimap.h (public API),
//        src/runtime/collections/rt_seq.h (value list backing type)
//
//===----------------------------------------------------------------------===//

#include "rt_multimap.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#define MM_INITIAL_CAPACITY 16
#define MM_LOAD_FACTOR_NUM 3
#define MM_LOAD_FACTOR_DEN 4
#include "rt_hash_util.h"

typedef struct rt_mm_entry
{
    char *key;
    size_t key_len;
    void *values; // Seq of values for this key
    struct rt_mm_entry *next;
} rt_mm_entry;

typedef struct rt_multimap_impl
{
    void **vptr;
    rt_mm_entry **buckets;
    size_t capacity;
    size_t key_count;
    size_t total_count;
} rt_multimap_impl;

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

static rt_mm_entry *find_entry(rt_mm_entry *head, const char *key, size_t key_len)
{
    for (rt_mm_entry *e = head; e; e = e->next)
    {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

static void free_entry(rt_mm_entry *entry)
{
    if (!entry)
        return;
    free(entry->key);
    if (entry->values && rt_obj_release_check0(entry->values))
        rt_obj_free(entry->values);
    free(entry);
}

static void mm_resize(rt_multimap_impl *mm, size_t new_cap)
{
    rt_mm_entry **new_buckets = (rt_mm_entry **)calloc(new_cap, sizeof(rt_mm_entry *));
    if (!new_buckets)
        return;
    for (size_t i = 0; i < mm->capacity; ++i)
    {
        rt_mm_entry *e = mm->buckets[i];
        while (e)
        {
            rt_mm_entry *next = e->next;
            uint64_t h = rt_fnv1a(e->key, e->key_len);
            size_t idx = h % new_cap;
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }
    free(mm->buckets);
    mm->buckets = new_buckets;
    mm->capacity = new_cap;
}

static void maybe_resize(rt_multimap_impl *mm)
{
    if (mm->key_count * MM_LOAD_FACTOR_DEN > mm->capacity * MM_LOAD_FACTOR_NUM)
        mm_resize(mm, mm->capacity * 2);
}

static void rt_multimap_finalize(void *obj)
{
    if (!obj)
        return;
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    if (!mm->buckets)
        return;
    rt_multimap_clear(mm);
    free(mm->buckets);
    mm->buckets = NULL;
    mm->capacity = 0;
}

void *rt_multimap_new(void)
{
    rt_multimap_impl *mm = (rt_multimap_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_multimap_impl));
    if (!mm)
        return NULL;
    mm->vptr = NULL;
    mm->buckets = (rt_mm_entry **)calloc(MM_INITIAL_CAPACITY, sizeof(rt_mm_entry *));
    if (!mm->buckets)
    {
        mm->capacity = 0;
        mm->key_count = 0;
        mm->total_count = 0;
        rt_obj_set_finalizer(mm, rt_multimap_finalize);
        return mm;
    }
    mm->capacity = MM_INITIAL_CAPACITY;
    mm->key_count = 0;
    mm->total_count = 0;
    rt_obj_set_finalizer(mm, rt_multimap_finalize);
    return mm;
}

int64_t rt_multimap_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_multimap_impl *)obj)->total_count;
}

int64_t rt_multimap_key_count(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_multimap_impl *)obj)->key_count;
}

int8_t rt_multimap_is_empty(void *obj)
{
    return rt_multimap_len(obj) == 0;
}

void rt_multimap_put(void *obj, rt_string key, void *value)
{
    if (!obj)
        return;
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    if (mm->capacity == 0)
        return;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry *existing = find_entry(mm->buckets[idx], key_data, key_len);
    if (existing)
    {
        rt_seq_push(existing->values, value);
        mm->total_count++;
        return;
    }

    // New key
    rt_mm_entry *entry = (rt_mm_entry *)malloc(sizeof(rt_mm_entry));
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

    entry->values = rt_seq_new();
    rt_obj_retain_maybe(entry->values);
    rt_seq_push(entry->values, value);

    entry->next = mm->buckets[idx];
    mm->buckets[idx] = entry;
    mm->key_count++;
    mm->total_count++;
    maybe_resize(mm);
}

void *rt_multimap_get(void *obj, rt_string key)
{
    if (!obj)
        return rt_seq_new();
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    if (mm->capacity == 0)
        return rt_seq_new();

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry *entry = find_entry(mm->buckets[idx], key_data, key_len);
    if (!entry)
        return rt_seq_new();

    // Return a copy of the values Seq
    void *result = rt_seq_new();
    int64_t len = rt_seq_len(entry->values);
    for (int64_t i = 0; i < len; ++i)
        rt_seq_push(result, rt_seq_get(entry->values, i));
    return result;
}

void *rt_multimap_get_first(void *obj, rt_string key)
{
    if (!obj)
        return NULL;
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    if (mm->capacity == 0)
        return NULL;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry *entry = find_entry(mm->buckets[idx], key_data, key_len);
    if (!entry || rt_seq_len(entry->values) == 0)
        return NULL;
    return rt_seq_get(entry->values, 0);
}

int8_t rt_multimap_has(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    if (mm->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;
    return find_entry(mm->buckets[idx], key_data, key_len) ? 1 : 0;
}

int64_t rt_multimap_count_for(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    if (mm->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry *entry = find_entry(mm->buckets[idx], key_data, key_len);
    return entry ? rt_seq_len(entry->values) : 0;
}

int8_t rt_multimap_remove_all(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    if (mm->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry **prev_ptr = &mm->buckets[idx];
    rt_mm_entry *entry = mm->buckets[idx];
    while (entry)
    {
        if (entry->key_len == key_len && memcmp(entry->key, key_data, key_len) == 0)
        {
            *prev_ptr = entry->next;
            mm->total_count -= (size_t)rt_seq_len(entry->values);
            mm->key_count--;
            free_entry(entry);
            return 1;
        }
        prev_ptr = &entry->next;
        entry = entry->next;
    }
    return 0;
}

void rt_multimap_clear(void *obj)
{
    if (!obj)
        return;
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    for (size_t i = 0; i < mm->capacity; ++i)
    {
        rt_mm_entry *entry = mm->buckets[i];
        while (entry)
        {
            rt_mm_entry *next = entry->next;
            free_entry(entry);
            entry = next;
        }
        mm->buckets[i] = NULL;
    }
    mm->key_count = 0;
    mm->total_count = 0;
}

void *rt_multimap_keys(void *obj)
{
    void *result = rt_seq_new();
    if (!obj)
        return result;
    rt_multimap_impl *mm = (rt_multimap_impl *)obj;
    for (size_t i = 0; i < mm->capacity; ++i)
    {
        for (rt_mm_entry *e = mm->buckets[i]; e; e = e->next)
        {
            rt_string ks = rt_string_from_bytes(e->key, e->key_len);
            rt_seq_push(result, (void *)ks);
        }
    }
    return result;
}
