//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_defaultmap.c
// Purpose: Implements a string-keyed hash map with a configurable default value
//   returned when a key is not present. DefaultMap behaves like a regular Map
//   for Set/Remove/Contains, but Get returns the default_value (set at creation)
//   instead of NULL for missing keys. Useful for counters, accumulators, and
//   lookup tables where a "zero" or sentinel default is needed.
//
// Key invariants:
//   - Backed by a hash table with initial capacity 16 buckets and separate
//     chaining using FNV-1a hashing.
//   - Resizes (doubles) when count/capacity exceeds 75%.
//   - Get returns default_value (NOT a copy) for missing keys; callers must
//     not mutate the returned default object.
//   - The default_value pointer is stored at construction time and is NOT
//     retained by the DefaultMap (caller must keep it alive).
//   - Each entry owns a heap-copied key string; values are stored as raw
//     pointers (not retained by the map).
//   - All operations are O(1) average case; O(n) worst case.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - DefaultMap objects are GC-managed (rt_obj_new_i64). The bucket array
//     and all entry nodes are freed by the GC finalizer.
//
// Links: src/runtime/collections/rt_defaultmap.h (public API),
//        src/runtime/collections/rt_map.h (standard map without default value)
//
//===----------------------------------------------------------------------===//

#include "rt_defaultmap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

// ---------------------------------------------------------------------------
// Internal structure
// ---------------------------------------------------------------------------

typedef struct rt_dm_entry
{
    char *key;
    size_t key_len;
    void *value;
    struct rt_dm_entry *next;
} rt_dm_entry;

typedef struct
{
    void *vptr;
    rt_dm_entry **buckets;
    int64_t capacity;
    int64_t count;
    void *default_value;
} rt_defaultmap_impl;

// ---------------------------------------------------------------------------
// Hash helper
// ---------------------------------------------------------------------------

static uint64_t dm_hash(const char *key, size_t len)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++)
    {
        h ^= (uint64_t)(unsigned char)key[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

static void dm_resize(rt_defaultmap_impl *m)
{
    int64_t new_cap = m->capacity * 2;
    rt_dm_entry **new_buckets = (rt_dm_entry **)calloc((size_t)new_cap, sizeof(rt_dm_entry *));
    if (!new_buckets)
        return;

    for (int64_t i = 0; i < m->capacity; i++)
    {
        rt_dm_entry *e = m->buckets[i];
        while (e)
        {
            rt_dm_entry *next = e->next;
            uint64_t idx = dm_hash(e->key, e->key_len) % (uint64_t)new_cap;
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }

    free(m->buckets);
    m->buckets = new_buckets;
    m->capacity = new_cap;
}

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

static void defaultmap_finalizer(void *obj)
{
    rt_defaultmap_impl *m = (rt_defaultmap_impl *)obj;
    for (int64_t i = 0; i < m->capacity; i++)
    {
        rt_dm_entry *e = m->buckets[i];
        while (e)
        {
            rt_dm_entry *next = e->next;
            free(e->key);
            if (e->value)
                rt_obj_release_check0(e->value);
            free(e);
            e = next;
        }
    }
    free(m->buckets);
    if (m->default_value)
        rt_obj_release_check0(m->default_value);
    m->buckets = NULL;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

void *rt_defaultmap_new(void *default_value)
{
    rt_defaultmap_impl *m = (rt_defaultmap_impl *)rt_obj_new_i64(0, sizeof(rt_defaultmap_impl));
    m->capacity = 16;
    m->count = 0;
    m->buckets = (rt_dm_entry **)calloc(16, sizeof(rt_dm_entry *));
    if (!m->buckets)
    {
        rt_trap("DefaultMap: memory allocation failed");
        return NULL;
    }
    m->default_value = default_value;
    if (default_value)
        rt_obj_retain_maybe(default_value);
    rt_obj_set_finalizer(m, defaultmap_finalizer);
    return m;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int64_t rt_defaultmap_len(void *map)
{
    if (!map)
        return 0;
    return ((rt_defaultmap_impl *)map)->count;
}

// ---------------------------------------------------------------------------
// Get (returns default if missing)
// ---------------------------------------------------------------------------

void *rt_defaultmap_get(void *map, rt_string key)
{
    if (!map || !key)
        return NULL;
    rt_defaultmap_impl *m = (rt_defaultmap_impl *)map;

    const char *kstr = rt_string_cstr(key);
    if (!kstr)
        return m->default_value;
    size_t klen = strlen(kstr);

    uint64_t idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    rt_dm_entry *e = m->buckets[idx];
    while (e)
    {
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0)
            return e->value;
        e = e->next;
    }
    return m->default_value;
}

// ---------------------------------------------------------------------------
// Set
// ---------------------------------------------------------------------------

void rt_defaultmap_set(void *map, rt_string key, void *value)
{
    if (!map || !key)
        return;
    rt_defaultmap_impl *m = (rt_defaultmap_impl *)map;

    const char *kstr = rt_string_cstr(key);
    if (!kstr)
        return;
    size_t klen = strlen(kstr);

    uint64_t idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    rt_dm_entry *e = m->buckets[idx];
    while (e)
    {
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0)
        {
            if (value)
                rt_obj_retain_maybe(value);
            if (e->value)
                rt_obj_release_check0(e->value);
            e->value = value;
            return;
        }
        e = e->next;
    }

    // Resize check
    if (m->count * 4 >= m->capacity * 3)
    {
        dm_resize(m);
        idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    }

    // New entry
    rt_dm_entry *ne = (rt_dm_entry *)calloc(1, sizeof(rt_dm_entry));
    if (!ne)
        rt_trap("rt_defaultmap: memory allocation failed");
    ne->key = (char *)malloc(klen + 1);
    if (!ne->key)
        rt_trap("rt_defaultmap: memory allocation failed");
    memcpy(ne->key, kstr, klen + 1);
    ne->key_len = klen;
    if (value)
        rt_obj_retain_maybe(value);
    ne->value = value;
    ne->next = m->buckets[idx];
    m->buckets[idx] = ne;
    m->count++;
}

// ---------------------------------------------------------------------------
// Has / Remove
// ---------------------------------------------------------------------------

int64_t rt_defaultmap_has(void *map, rt_string key)
{
    if (!map || !key)
        return 0;
    rt_defaultmap_impl *m = (rt_defaultmap_impl *)map;

    const char *kstr = rt_string_cstr(key);
    if (!kstr)
        return 0;
    size_t klen = strlen(kstr);

    uint64_t idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    rt_dm_entry *e = m->buckets[idx];
    while (e)
    {
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0)
            return 1;
        e = e->next;
    }
    return 0;
}

int8_t rt_defaultmap_remove(void *map, rt_string key)
{
    if (!map || !key)
        return 0;
    rt_defaultmap_impl *m = (rt_defaultmap_impl *)map;

    const char *kstr = rt_string_cstr(key);
    if (!kstr)
        return 0;
    size_t klen = strlen(kstr);

    uint64_t idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    rt_dm_entry **pp = &m->buckets[idx];
    while (*pp)
    {
        rt_dm_entry *e = *pp;
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0)
        {
            *pp = e->next;
            free(e->key);
            if (e->value)
                rt_obj_release_check0(e->value);
            free(e);
            m->count--;
            return 1;
        }
        pp = &e->next;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

void *rt_defaultmap_keys(void *map)
{
    void *seq = rt_seq_new();
    if (!map)
        return seq;
    rt_defaultmap_impl *m = (rt_defaultmap_impl *)map;

    for (int64_t i = 0; i < m->capacity; i++)
    {
        rt_dm_entry *e = m->buckets[i];
        while (e)
        {
            rt_string k = rt_string_from_bytes(e->key, e->key_len);
            rt_seq_push(seq, k);
            e = e->next;
        }
    }
    return seq;
}

// ---------------------------------------------------------------------------
// Get default / Clear
// ---------------------------------------------------------------------------

void *rt_defaultmap_get_default(void *map)
{
    if (!map)
        return NULL;
    return ((rt_defaultmap_impl *)map)->default_value;
}

void rt_defaultmap_clear(void *map)
{
    if (!map)
        return;
    rt_defaultmap_impl *m = (rt_defaultmap_impl *)map;

    for (int64_t i = 0; i < m->capacity; i++)
    {
        rt_dm_entry *e = m->buckets[i];
        while (e)
        {
            rt_dm_entry *next = e->next;
            free(e->key);
            if (e->value)
                rt_obj_release_check0(e->value);
            free(e);
            e = next;
        }
        m->buckets[i] = NULL;
    }
    m->count = 0;
}

int8_t rt_defaultmap_is_empty(void *map)
{
    if (!map)
        return 1;
    return ((rt_defaultmap_impl *)map)->count == 0 ? 1 : 0;
}
