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

#include "rt_collection_ids.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MM_INITIAL_CAPACITY 16
#define MM_LOAD_FACTOR_NUM 3
#define MM_LOAD_FACTOR_DEN 4
#include "rt_hash_util.h"

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

typedef struct rt_mm_entry {
    char *key;
    size_t key_len;
    void *values; // Seq of values for this key
    struct rt_mm_entry *next;
} rt_mm_entry;

typedef struct rt_multimap_impl {
    void **vptr;
    rt_mm_entry **buckets;
    size_t capacity;
    size_t key_count;
    size_t total_count;
} rt_multimap_impl;

/// @brief Checked cast of an opaque handle to the MultiMap implementation.
/// @details Traps with @p what if @p obj is NULL or not a MultiMap.
static rt_multimap_impl *as_multimap(void *obj, const char *what) {
    if (!obj || rt_obj_class_id(obj) != RT_MULTIMAP_CLASS_ID)
        rt_trap(what);
    return (rt_multimap_impl *)obj;
}

/// @brief Borrow the byte buffer + length of a key string (empty "" if null).
static const char *get_key_data(rt_string key, size_t *out_len) {
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

/// @brief Linear scan of a bucket chain for an exact key match (NULL if none).
static rt_mm_entry *find_entry(rt_mm_entry *head, const char *key, size_t key_len) {
    for (rt_mm_entry *e = head; e; e = e->next) {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

/// @brief Drop a GC-managed temporary object and free it at zero refs.
static void mm_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Free a chain entry: its key buffer and its retained values list.
static void free_entry(rt_mm_entry *entry) {
    if (!entry)
        return;
    free(entry->key);
    mm_release_object(entry->values);
    free(entry);
}

static void mm_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    const char *err = rt_trap_get_error();
    snprintf(buffer, buffer_size, "%s", err && err[0] ? err : fallback);
}

static void mm_push_value_or_release_entry(rt_mm_entry *entry, void *value) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        mm_save_trap_error(saved_error, sizeof(saved_error), "MultiMap: value retain failed");
        rt_trap_clear_recovery();
        free_entry(entry);
        rt_trap(saved_error);
        return;
    }

    rt_seq_push(entry->values, value);
    rt_trap_clear_recovery();
}

static void mm_push_value_or_release_seq(void *seq, void *value, const char *fallback) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        mm_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        mm_release_object(seq);
        rt_trap(saved_error);
        return;
    }

    rt_seq_push(seq, value);
    rt_trap_clear_recovery();
}

static void mm_append_key_or_release_seq(void *seq, const char *key, size_t key_len) {
    volatile rt_string key_copy = NULL;

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        mm_save_trap_error(saved_error, sizeof(saved_error), "MultiMap.Keys: failed to copy key");
        rt_trap_clear_recovery();
        if (key_copy)
            rt_str_release_maybe((rt_string)key_copy);
        mm_release_object(seq);
        rt_trap(saved_error);
        return;
    }

    key_copy = rt_string_from_bytes(key, key_len);
    rt_seq_push(seq, (void *)key_copy);
    rt_str_release_maybe((rt_string)key_copy);
    key_copy = NULL;
    rt_trap_clear_recovery();
}

/// @brief Rehash all entries into a fresh @p new_cap bucket array.
/// @details Traps on allocation overflow/OOM.
static void mm_resize(rt_multimap_impl *mm, size_t new_cap) {
    if (new_cap > SIZE_MAX / sizeof(rt_mm_entry *))
        rt_trap("MultiMap: allocation size overflow");
    rt_mm_entry **new_buckets = (rt_mm_entry **)calloc(new_cap, sizeof(rt_mm_entry *));
    if (!new_buckets)
        rt_trap("MultiMap: memory allocation failed");
    for (size_t i = 0; i < mm->capacity; ++i) {
        rt_mm_entry *e = mm->buckets[i];
        while (e) {
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

/// @brief Double the bucket array once the key load factor exceeds the
///        MM_LOAD_FACTOR_NUM/MM_LOAD_FACTOR_DEN threshold (capped).
static void maybe_resize_for_insert(rt_multimap_impl *mm) {
    size_t next_count = mm->key_count + 1;
    if ((long double)next_count * (long double)MM_LOAD_FACTOR_DEN >
        (long double)mm->capacity * (long double)MM_LOAD_FACTOR_NUM) {
        if (mm->capacity > SIZE_MAX / 2)
            return;
        mm_resize(mm, mm->capacity * 2);
    }
}

/// @brief GC finalizer: clear all entries (via rt_multimap_clear) then free
///        the bucket array.
static void rt_multimap_finalize(void *obj) {
    if (!obj)
        return;
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap: invalid MultiMap object");
    if (!mm->buckets)
        return;
    rt_multimap_clear(mm);
    free(mm->buckets);
    mm->buckets = NULL;
    mm->capacity = 0;
}

/// @brief GC traversal: visit each key's values-list across all chains.
static void rt_multimap_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    if (!obj || !visitor)
        return;
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap: invalid MultiMap object");
    if (!mm->buckets || mm->capacity == 0)
        return;
    for (size_t i = 0; i < mm->capacity; ++i) {
        for (rt_mm_entry *entry = mm->buckets[i]; entry; entry = entry->next)
            visitor(entry->values, ctx);
    }
}

/// @brief Construct an empty multimap (string → list-of-values). Internal storage is a chained
/// hash table where each bucket holds a Seq of values per key. Useful when one key maps to
/// many values (URL params, headers, multiset, etc.).
void *rt_multimap_new(void) {
    rt_multimap_impl *mm =
        (rt_multimap_impl *)rt_obj_new_i64(RT_MULTIMAP_CLASS_ID, (int64_t)sizeof(rt_multimap_impl));
    if (!mm)
        rt_trap("MultiMap: memory allocation failed");
    mm->vptr = NULL;
    mm->buckets = (rt_mm_entry **)calloc(MM_INITIAL_CAPACITY, sizeof(rt_mm_entry *));
    if (!mm->buckets) {
        if (rt_obj_release_check0(mm))
            rt_obj_free(mm);
        rt_trap("MultiMap: memory allocation failed");
    }
    mm->capacity = MM_INITIAL_CAPACITY;
    mm->key_count = 0;
    mm->total_count = 0;
    rt_obj_set_finalizer(mm, rt_multimap_finalize);
    rt_gc_track(mm, rt_multimap_traverse);
    return mm;
}

/// @brief Return the total number of values across all keys in the multimap.
int64_t rt_multimap_len(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)as_multimap(obj, "MultiMap.Len: invalid MultiMap object")->total_count;
}

/// @brief Return the number of distinct keys in the multimap.
/// @param obj Multimap object pointer; returns 0 if NULL.
/// @return Number of unique keys.
int64_t rt_multimap_key_count(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)as_multimap(obj, "MultiMap.KeyCount: invalid MultiMap object")->key_count;
}

/// @brief Check whether the multimap has no entries.
int8_t rt_multimap_is_empty(void *obj) {
    return rt_multimap_len(obj) == 0;
}

/// @brief Add a value under a key in the multimap.
/// @details Unlike a regular map, multimaps allow multiple values per key.
///          The value is appended to the key's value list.
void rt_multimap_put(void *obj, rt_string key, void *value) {
    if (!obj)
        return;
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap.Put: invalid MultiMap object");
    if (mm->capacity == 0)
        rt_trap("MultiMap: finalized MultiMap object");

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry *existing = find_entry(mm->buckets[idx], key_data, key_len);
    if (existing) {
        if (mm->total_count >= (size_t)INT64_MAX)
            rt_trap("MultiMap: length overflow");
        rt_seq_push(existing->values, value);
        mm->total_count++;
        return;
    }

    // New key
    if (mm->key_count >= (size_t)INT64_MAX || mm->total_count >= (size_t)INT64_MAX)
        rt_trap("MultiMap: length overflow");
    maybe_resize_for_insert(mm);
    idx = hash % mm->capacity;

    void *values = rt_seq_new_owned();
    rt_mm_entry *entry = (rt_mm_entry *)malloc(sizeof(rt_mm_entry));
    if (!entry) {
        mm_release_object(values);
        rt_trap("MultiMap: memory allocation failed");
    }
    if (key_len == SIZE_MAX) {
        mm_release_object(values);
        free(entry);
        rt_trap("MultiMap: key allocation overflow");
    }
    entry->key = (char *)malloc(key_len + 1);
    if (!entry->key) {
        mm_release_object(values);
        free(entry);
        rt_trap("MultiMap: key allocation failed");
    }
    memcpy(entry->key, key_data, key_len);
    entry->key[key_len] = '\0';
    entry->key_len = key_len;

    entry->values = values;
    entry->next = NULL;
    // The Seq object itself starts with refcount=1; do not retain it again.
    mm_push_value_or_release_entry(entry, value);

    entry->next = mm->buckets[idx];
    mm->buckets[idx] = entry;
    mm->key_count++;
    mm->total_count++;
}

/// @brief Return all values stored under `key` as a fresh Seq (not a borrowed view). Empty
/// Seq if key is absent. Useful when caller may mutate the returned list independently.
void *rt_multimap_get(void *obj, rt_string key) {
    if (!obj)
        return rt_seq_new_owned();
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap.Get: invalid MultiMap object");
    if (mm->capacity == 0)
        return rt_seq_new_owned();

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry *entry = find_entry(mm->buckets[idx], key_data, key_len);
    if (!entry)
        return rt_seq_new_owned();

    // Return a copy of the values Seq
    void *result = rt_seq_new_owned();
    int64_t len = rt_seq_len(entry->values);
    for (int64_t i = 0; i < len; ++i) {
        void *value = rt_seq_get(entry->values, i);
        mm_push_value_or_release_seq(result, value, "MultiMap.Get: failed to copy values");
    }
    return result;
}

/// @brief Return the first value stored under `key` (most recently inserted is at the tail —
/// this returns the *first* inserted). NULL if the key is absent or has no values.
void *rt_multimap_get_first(void *obj, rt_string key) {
    if (!obj)
        return NULL;
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap.GetFirst: invalid MultiMap object");
    if (mm->capacity == 0)
        return NULL;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry *entry = find_entry(mm->buckets[idx], key_data, key_len);
    if (!entry || rt_seq_len(entry->values) == 0)
        return NULL;
    void *value = rt_seq_get(entry->values, 0);
    rt_obj_retain_maybe(value);
    return value;
}

/// @brief Check whether a key exists in the multimap.
int8_t rt_multimap_has(void *obj, rt_string key) {
    if (!obj)
        return 0;
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap.Has: invalid MultiMap object");
    if (mm->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;
    return find_entry(mm->buckets[idx], key_data, key_len) ? 1 : 0;
}

/// @brief Return the number of values associated with a specific key.
/// @param obj Multimap object pointer; returns 0 if NULL.
/// @param key Key to count values for.
/// @return Number of values stored under the key, 0 if key not found.
int64_t rt_multimap_count_for(void *obj, rt_string key) {
    if (!obj)
        return 0;
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap.CountFor: invalid MultiMap object");
    if (mm->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry *entry = find_entry(mm->buckets[idx], key_data, key_len);
    return entry ? rt_seq_len(entry->values) : 0;
}

/// @brief Remove all values associated with a key from the multimap.
/// @details Frees the key's entry and releases all value references in its list.
/// @param obj Multimap object pointer; returns 0 if NULL.
/// @param key Key whose entries should be removed.
/// @return 1 if the key was found and removed, 0 otherwise.
int8_t rt_multimap_remove_all(void *obj, rt_string key) {
    if (!obj)
        return 0;
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap.RemoveAll: invalid MultiMap object");
    if (mm->capacity == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % mm->capacity;

    rt_mm_entry **prev_ptr = &mm->buckets[idx];
    rt_mm_entry *entry = mm->buckets[idx];
    while (entry) {
        if (entry->key_len == key_len && memcmp(entry->key, key_data, key_len) == 0) {
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

/// @brief Remove all entries from the multimap.
void rt_multimap_clear(void *obj) {
    if (!obj)
        return;
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap.Clear: invalid MultiMap object");
    for (size_t i = 0; i < mm->capacity; ++i) {
        rt_mm_entry *entry = mm->buckets[i];
        while (entry) {
            rt_mm_entry *next = entry->next;
            free_entry(entry);
            entry = next;
        }
        mm->buckets[i] = NULL;
    }
    mm->key_count = 0;
    mm->total_count = 0;
}

/// @brief Return a Seq of every distinct key in the multimap (bucket-iteration order).
void *rt_multimap_keys(void *obj) {
    if (!obj) {
        void *empty = rt_seq_new();
        rt_seq_set_owns_elements(empty, 1);
        return empty;
    }
    rt_multimap_impl *mm = as_multimap(obj, "MultiMap.Keys: invalid MultiMap object");
    void *result = rt_seq_new();
    rt_seq_set_owns_elements(result, 1);
    for (size_t i = 0; i < mm->capacity; ++i) {
        for (rt_mm_entry *e = mm->buckets[i]; e; e = e->next) {
            mm_append_key_or_release_seq(result, e->key, e->key_len);
        }
    }
    return result;
}
