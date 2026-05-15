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
//   - The default_value pointer is retained at construction time and released
//     by the finalizer.
//   - Each entry owns a heap-copied key string; values are retained by the map
//     and released on removal or finalization.
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
#include "rt_collection_ids.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

// ---------------------------------------------------------------------------
// Internal structure
// ---------------------------------------------------------------------------

typedef struct rt_dm_entry {
    char *key;
    size_t key_len;
    void *value;
    struct rt_dm_entry *next;
} rt_dm_entry;

typedef struct {
    void *vptr;
    rt_dm_entry **buckets;
    int64_t capacity;
    int64_t count;
    void *default_value;
} rt_defaultmap_impl;

static rt_defaultmap_impl *as_defaultmap(void *obj, const char *what) {
    if (!obj || rt_obj_class_id(obj) != RT_DEFAULTMAP_CLASS_ID)
        rt_trap(what);
    return (rt_defaultmap_impl *)obj;
}

// ---------------------------------------------------------------------------
// Hash helper
// ---------------------------------------------------------------------------

static uint64_t dm_hash(const char *key, size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)(unsigned char)key[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static const char *dm_key_data(rt_string key, size_t *out_len) {
    if (!key) {
        *out_len = 0;
        return "";
    }
    int64_t len = rt_str_len(key);
    if (len <= 0) {
        *out_len = 0;
        return "";
    }
    const char *cstr = rt_string_cstr(key);
    if (!cstr) {
        *out_len = 0;
        return "";
    }
    *out_len = (size_t)len;
    return cstr;
}

static void dm_release_value(void *value) {
    if (value && rt_obj_release_check0(value))
        rt_obj_free(value);
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

static void dm_resize(rt_defaultmap_impl *m) {
    if (m->capacity > INT64_MAX / 2)
        return;
    int64_t new_cap = m->capacity * 2;
    if ((uint64_t)new_cap > SIZE_MAX / sizeof(rt_dm_entry *))
        rt_trap("DefaultMap: allocation size overflow");
    rt_dm_entry **new_buckets = (rt_dm_entry **)calloc((size_t)new_cap, sizeof(rt_dm_entry *));
    if (!new_buckets)
        rt_trap("DefaultMap: memory allocation failed");

    for (int64_t i = 0; i < m->capacity; i++) {
        rt_dm_entry *e = m->buckets[i];
        while (e) {
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

static int dm_should_resize(rt_defaultmap_impl *m) {
    return (long double)m->count * 4.0L >= (long double)m->capacity * 3.0L;
}

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

static void defaultmap_finalizer(void *obj) {
    if (!obj)
        return;
    rt_defaultmap_impl *m = as_defaultmap(obj, "DefaultMap: invalid DefaultMap object");
    if (!m->buckets)
        return;
    for (int64_t i = 0; i < m->capacity; i++) {
        rt_dm_entry *e = m->buckets[i];
        while (e) {
            rt_dm_entry *next = e->next;
            free(e->key);
            dm_release_value(e->value);
            free(e);
            e = next;
        }
    }
    free(m->buckets);
    dm_release_value(m->default_value);
    m->buckets = NULL;
    m->capacity = 0;
    m->count = 0;
    m->default_value = NULL;
}

static void defaultmap_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    if (!obj || !visitor)
        return;
    rt_defaultmap_impl *m = as_defaultmap(obj, "DefaultMap: invalid DefaultMap object");
    visitor(m->default_value, ctx);
    if (!m->buckets)
        return;
    for (int64_t i = 0; i < m->capacity; i++) {
        for (rt_dm_entry *e = m->buckets[i]; e; e = e->next)
            visitor(e->value, ctx);
    }
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

void *rt_defaultmap_new(void *default_value) {
    rt_defaultmap_impl *m =
        (rt_defaultmap_impl *)rt_obj_new_i64(RT_DEFAULTMAP_CLASS_ID, sizeof(rt_defaultmap_impl));
    if (!m)
        rt_trap("DefaultMap: memory allocation failed");
    m->capacity = 16;
    m->count = 0;
    m->default_value = NULL;
    m->buckets = (rt_dm_entry **)calloc(16, sizeof(rt_dm_entry *));
    if (!m->buckets) {
        if (rt_obj_release_check0(m))
            rt_obj_free(m);
        rt_trap("DefaultMap: memory allocation failed");
    }
    m->default_value = default_value;
    if (default_value)
        rt_obj_retain_maybe(default_value);
    rt_obj_set_finalizer(m, defaultmap_finalizer);
    rt_gc_track(m, defaultmap_traverse);
    return m;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/// @brief Return the number of key-value pairs in the default map.
int64_t rt_defaultmap_len(void *map) {
    if (!map)
        return 0;
    return as_defaultmap(map, "DefaultMap.Len: invalid DefaultMap object")->count;
}

// ---------------------------------------------------------------------------
// Get (returns default if missing)
// ---------------------------------------------------------------------------

void *rt_defaultmap_get(void *map, rt_string key) {
    if (!map)
        return NULL;
    rt_defaultmap_impl *m = as_defaultmap(map, "DefaultMap.Get: invalid DefaultMap object");

    size_t klen;
    const char *kstr = dm_key_data(key, &klen);

    uint64_t idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    rt_dm_entry *e = m->buckets[idx];
    while (e) {
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0)
            return e->value;
        e = e->next;
    }
    return m->default_value;
}

// ---------------------------------------------------------------------------
// Set
// ---------------------------------------------------------------------------

/// @brief Insert or update a key-value pair in the default map.
/// @details If the key already exists, the old value is released and replaced
///          with the new one. Both key and value are retained by the map.
void rt_defaultmap_set(void *map, rt_string key, void *value) {
    if (!map)
        return;
    rt_defaultmap_impl *m = as_defaultmap(map, "DefaultMap.Set: invalid DefaultMap object");

    size_t klen;
    const char *kstr = dm_key_data(key, &klen);

    uint64_t idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    rt_dm_entry *e = m->buckets[idx];
    while (e) {
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0) {
            if (value)
                rt_obj_retain_maybe(value);
            dm_release_value(e->value);
            e->value = value;
            return;
        }
        e = e->next;
    }

    // Resize check
    if (dm_should_resize(m)) {
        dm_resize(m);
        idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    }

    // New entry
    rt_dm_entry *ne = (rt_dm_entry *)calloc(1, sizeof(rt_dm_entry));
    if (!ne)
        rt_trap("rt_defaultmap: memory allocation failed");
    if (klen == SIZE_MAX) {
        free(ne);
        rt_trap("rt_defaultmap: key allocation overflow");
    }
    ne->key = (char *)malloc(klen + 1);
    if (!ne->key) {
        free(ne);
        rt_trap("rt_defaultmap: memory allocation failed");
    }
    memcpy(ne->key, kstr, klen);
    ne->key[klen] = '\0';
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

/// @brief Check whether a key exists in the default map.
/// @details Hashes the key with FNV-1a, indexes into the bucket array, and
///          walks the separate-chaining linked list comparing by key length
///          and content. Returns 1 if found, 0 otherwise.
int64_t rt_defaultmap_has(void *map, rt_string key) {
    if (!map)
        return 0;
    rt_defaultmap_impl *m = as_defaultmap(map, "DefaultMap.Has: invalid DefaultMap object");

    size_t klen;
    const char *kstr = dm_key_data(key, &klen);

    uint64_t idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    rt_dm_entry *e = m->buckets[idx];
    while (e) {
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0)
            return 1;
        e = e->next;
    }
    return 0;
}

/// @brief Remove a key-value pair from the default map.
/// @details Walks the bucket's chain using a pointer-to-pointer technique to
///          unlink the entry. Releases the value reference (if non-null) and
///          frees the key string and entry node.
int8_t rt_defaultmap_remove(void *map, rt_string key) {
    if (!map)
        return 0;
    rt_defaultmap_impl *m = as_defaultmap(map, "DefaultMap.Remove: invalid DefaultMap object");

    size_t klen;
    const char *kstr = dm_key_data(key, &klen);

    uint64_t idx = dm_hash(kstr, klen) % (uint64_t)m->capacity;
    rt_dm_entry **pp = &m->buckets[idx];
    while (*pp) {
        rt_dm_entry *e = *pp;
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0) {
            *pp = e->next;
            free(e->key);
            dm_release_value(e->value);
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

void *rt_defaultmap_keys(void *map) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!map)
        return seq;
    rt_defaultmap_impl *m = as_defaultmap(map, "DefaultMap.Keys: invalid DefaultMap object");

    for (int64_t i = 0; i < m->capacity; i++) {
        rt_dm_entry *e = m->buckets[i];
        while (e) {
            rt_string k = rt_string_from_bytes(e->key, e->key_len);
            rt_seq_push(seq, k);
            rt_str_release_maybe(k);
            e = e->next;
        }
    }
    return seq;
}

// ---------------------------------------------------------------------------
// Get default / Clear
// ---------------------------------------------------------------------------

void *rt_defaultmap_get_default(void *map) {
    if (!map)
        return NULL;
    return as_defaultmap(map, "DefaultMap.GetDefault: invalid DefaultMap object")->default_value;
}

/// @brief Remove all entries from the default map.
/// @details Releases all retained key and value references and resets the
///          entry count to zero. The backing array is retained at its current
///          capacity to avoid re-allocation on subsequent inserts.
void rt_defaultmap_clear(void *map) {
    if (!map)
        return;
    rt_defaultmap_impl *m = as_defaultmap(map, "DefaultMap.Clear: invalid DefaultMap object");

    for (int64_t i = 0; i < m->capacity; i++) {
        rt_dm_entry *e = m->buckets[i];
        while (e) {
            rt_dm_entry *next = e->next;
            free(e->key);
            dm_release_value(e->value);
            free(e);
            e = next;
        }
        m->buckets[i] = NULL;
    }
    m->count = 0;
}

/// @brief Check whether the default map contains no entries.
/// @details Equivalent to checking if len == 0.
int8_t rt_defaultmap_is_empty(void *map) {
    if (!map)
        return 1;
    return as_defaultmap(map, "DefaultMap.IsEmpty: invalid DefaultMap object")->count == 0 ? 1 : 0;
}
