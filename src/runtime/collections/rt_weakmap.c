//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_weakmap.c
// Purpose: Implements a string-keyed map that holds zeroing weak references
//   to its values. Keys are retained rt_string handles; values are tracked by
//   rt_weakref so freeing a target automatically makes subsequent lookups
//   return NULL.
//
// Key invariants:
//   - Open-addressing hash table with initial capacity WM_INITIAL_CAP (16).
//   - FNV-1a hashes the raw key bytes, so embedded NUL bytes are part of keys.
//   - Load factor is kept below roughly 70% by doubling and rehashing.
//   - Values are stored as rt_weakref handles and are never strongly retained.
//   - `count` tracks occupied slots; public Len counts only live weak targets.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - WeakMap objects are GC-managed (rt_obj_new_i64).
//   - Entry keys are retained and released with the entry.
//   - Weak reference handles are owned by the map and freed on removal/clear.
//
// Links: src/runtime/collections/rt_weakmap.h (public API),
//        src/runtime/core/rt_gc.h (zeroing weak references)
//
//===----------------------------------------------------------------------===//

#include "rt_weakmap.h"

#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#define WM_INITIAL_CAP 16

typedef struct {
    rt_string key;
    rt_weakref *value_ref;
    int8_t occupied;
} wm_entry;

typedef struct {
    wm_entry *entries;
    int64_t capacity;
    int64_t count;
} rt_weakmap_data;

static const char *wm_key_data(rt_string key, size_t *out_len) {
    if (!key) {
        *out_len = 0;
        return "";
    }
    int64_t len = rt_str_len(key);
    if (len <= 0) {
        *out_len = 0;
        return "";
    }
    const char *data = rt_string_cstr(key);
    if (!data) {
        *out_len = 0;
        return "";
    }
    *out_len = (size_t)len;
    return data;
}

static uint64_t wm_hash_bytes(const char *data, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)(unsigned char)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int8_t wm_entry_matches(const wm_entry *entry, const char *key, size_t key_len) {
    if (!entry->occupied)
        return 0;
    size_t entry_len = 0;
    const char *entry_key = wm_key_data(entry->key, &entry_len);
    return entry_len == key_len && memcmp(entry_key, key, key_len) == 0 ? 1 : 0;
}

static int8_t wm_entry_alive(const wm_entry *entry) {
    return entry->occupied && rt_weakref_alive(entry->value_ref) ? 1 : 0;
}

static void wm_release_entry(wm_entry *entry) {
    if (!entry || !entry->occupied)
        return;
    rt_str_release_maybe(entry->key);
    rt_weakref_free(entry->value_ref);
    entry->key = NULL;
    entry->value_ref = NULL;
    entry->occupied = 0;
}

static wm_entry *wm_alloc_entries(int64_t capacity) {
    if (capacity <= 0 || (uint64_t)capacity > SIZE_MAX / sizeof(wm_entry))
        rt_trap("WeakMap: allocation size overflow");
    wm_entry *entries = (wm_entry *)calloc((size_t)capacity, sizeof(wm_entry));
    if (!entries)
        rt_trap("WeakMap: memory allocation failed");
    return entries;
}

static int64_t wm_find_slot(rt_weakmap_data *data, const char *key, size_t key_len) {
    uint64_t h = wm_hash_bytes(key, key_len);
    int64_t idx = (int64_t)(h % (uint64_t)data->capacity);
    for (int64_t i = 0; i < data->capacity; i++) {
        int64_t slot = (idx + i) % data->capacity;
        if (!data->entries[slot].occupied)
            return slot;
        if (wm_entry_matches(&data->entries[slot], key, key_len))
            return slot;
    }
    return -1;
}

static void wm_move_live_entry(rt_weakmap_data *data, wm_entry entry) {
    size_t key_len = 0;
    const char *key = wm_key_data(entry.key, &key_len);
    int64_t slot = wm_find_slot(data, key, key_len);
    if (slot < 0)
        rt_trap("WeakMap: rehash failed");
    data->entries[slot] = entry;
    data->count++;
}

static void wm_grow(rt_weakmap_data *data) {
    int64_t old_cap = data->capacity;
    wm_entry *old_entries = data->entries;

    if (old_cap > INT64_MAX / 2)
        rt_trap("WeakMap: capacity overflow");
    int64_t new_cap = old_cap * 2;
    wm_entry *new_entries = wm_alloc_entries(new_cap);

    data->entries = new_entries;
    data->capacity = new_cap;
    data->count = 0;

    for (int64_t i = 0; i < old_cap; i++) {
        if (!old_entries[i].occupied)
            continue;
        if (wm_entry_alive(&old_entries[i])) {
            wm_move_live_entry(data, old_entries[i]);
        } else {
            wm_release_entry(&old_entries[i]);
        }
    }
    free(old_entries);
}

static void weakmap_finalizer(void *obj) {
    rt_weakmap_data *data = (rt_weakmap_data *)obj;
    if (!data || !data->entries)
        return;
    for (int64_t i = 0; i < data->capacity; i++)
        wm_release_entry(&data->entries[i]);
    free(data->entries);
    data->entries = NULL;
    data->capacity = 0;
    data->count = 0;
}

void *rt_weakmap_new(void) {
    void *obj = rt_obj_new_i64(0, sizeof(rt_weakmap_data));
    rt_weakmap_data *data = (rt_weakmap_data *)obj;
    data->entries = wm_alloc_entries(WM_INITIAL_CAP);
    data->capacity = WM_INITIAL_CAP;
    data->count = 0;
    rt_obj_set_finalizer(obj, weakmap_finalizer);
    return obj;
}

int64_t rt_weakmap_len(void *map) {
    if (!map)
        return 0;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    int64_t live = 0;
    for (int64_t i = 0; i < data->capacity; i++) {
        if (wm_entry_alive(&data->entries[i]))
            live++;
    }
    return live;
}

int8_t rt_weakmap_is_empty(void *map) {
    return rt_weakmap_len(map) == 0 ? 1 : 0;
}

void rt_weakmap_set(void *map, rt_string key, void *value) {
    if (!map)
        return;
    rt_weakmap_data *data = (rt_weakmap_data *)map;

    if ((long double)data->count * 10.0L >= (long double)data->capacity * 7.0L)
        wm_grow(data);

    size_t key_len = 0;
    const char *key_data = wm_key_data(key, &key_len);
    int64_t slot = wm_find_slot(data, key_data, key_len);
    if (slot < 0)
        rt_trap("WeakMap: insertion failed");

    if (data->entries[slot].occupied) {
        rt_weakref_free(data->entries[slot].value_ref);
        data->entries[slot].value_ref = rt_weakref_new(value);
        return;
    }

    rt_string stored_key = key ? key : rt_str_empty();
    data->entries[slot].key = stored_key;
    rt_obj_retain_maybe(stored_key);
    data->entries[slot].value_ref = rt_weakref_new(value);
    data->entries[slot].occupied = 1;
    data->count++;
}

void *rt_weakmap_get(void *map, rt_string key) {
    if (!map)
        return NULL;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    size_t key_len = 0;
    const char *key_data = wm_key_data(key, &key_len);
    int64_t slot = wm_find_slot(data, key_data, key_len);
    if (slot < 0 || !data->entries[slot].occupied)
        return NULL;
    return rt_weakref_get(data->entries[slot].value_ref);
}

int8_t rt_weakmap_has(void *map, rt_string key) {
    if (!map)
        return 0;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    size_t key_len = 0;
    const char *key_data = wm_key_data(key, &key_len);
    int64_t slot = wm_find_slot(data, key_data, key_len);
    return slot >= 0 && wm_entry_alive(&data->entries[slot]) ? 1 : 0;
}

int8_t rt_weakmap_remove(void *map, rt_string key) {
    if (!map)
        return 0;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    size_t key_len = 0;
    const char *key_data = wm_key_data(key, &key_len);
    int64_t slot = wm_find_slot(data, key_data, key_len);
    if (slot < 0 || !data->entries[slot].occupied)
        return 0;

    wm_release_entry(&data->entries[slot]);
    data->count--;

    int64_t next = (slot + 1) % data->capacity;
    while (data->entries[next].occupied) {
        wm_entry tmp = data->entries[next];
        data->entries[next].key = NULL;
        data->entries[next].value_ref = NULL;
        data->entries[next].occupied = 0;
        data->count--;
        wm_move_live_entry(data, tmp);
        next = (next + 1) % data->capacity;
    }

    return 1;
}

void *rt_weakmap_keys(void *map) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!map)
        return seq;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    for (int64_t i = 0; i < data->capacity; i++) {
        if (wm_entry_alive(&data->entries[i]))
            rt_seq_push(seq, data->entries[i].key);
    }
    return seq;
}

void rt_weakmap_clear(void *map) {
    if (!map)
        return;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    for (int64_t i = 0; i < data->capacity; i++)
        wm_release_entry(&data->entries[i]);
    data->count = 0;
}

int64_t rt_weakmap_compact(void *map) {
    if (!map)
        return 0;
    rt_weakmap_data *data = (rt_weakmap_data *)map;

    int64_t removed = 0;
    for (int64_t i = 0; i < data->capacity; i++) {
        if (data->entries[i].occupied && !wm_entry_alive(&data->entries[i]))
            removed++;
    }
    if (removed == 0)
        return 0;

    wm_entry *old_entries = data->entries;
    int64_t old_cap = data->capacity;
    data->entries = wm_alloc_entries(old_cap);
    data->count = 0;

    for (int64_t i = 0; i < old_cap; i++) {
        if (!old_entries[i].occupied)
            continue;
        if (wm_entry_alive(&old_entries[i])) {
            wm_move_live_entry(data, old_entries[i]);
        } else {
            wm_release_entry(&old_entries[i]);
        }
    }
    free(old_entries);
    return removed;
}
