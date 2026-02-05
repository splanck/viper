//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_weakmap.h"

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

// --- Internal structure ---
// Simple open-addressing hash table with string keys and weak value pointers.
// Values are NOT retained - this is the "weak" semantics.

#define WM_INITIAL_CAP 16

typedef struct
{
    rt_string key;
    void *value; // NOT retained (weak reference)
    int8_t occupied;
} wm_entry;

typedef struct
{
    wm_entry *entries;
    int64_t capacity;
    int64_t count;
} rt_weakmap_data;

static uint64_t wm_hash_str(const char *s)
{
    uint64_t h = 14695981039346656037ULL;
    for (; *s; s++)
    {
        h ^= (uint64_t)(unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

static void wm_grow(rt_weakmap_data *data);

static int64_t wm_find_slot(rt_weakmap_data *data, const char *key_cstr)
{
    uint64_t h = wm_hash_str(key_cstr);
    int64_t idx = (int64_t)(h % (uint64_t)data->capacity);
    for (int64_t i = 0; i < data->capacity; i++)
    {
        int64_t slot = (idx + i) % data->capacity;
        if (!data->entries[slot].occupied)
            return slot;
        if (strcmp(rt_string_cstr(data->entries[slot].key), key_cstr) == 0)
            return slot;
    }
    return -1;
}

static void wm_grow(rt_weakmap_data *data)
{
    int64_t old_cap = data->capacity;
    wm_entry *old_entries = data->entries;

    data->capacity = old_cap * 2;
    data->entries = (wm_entry *)calloc((size_t)data->capacity, sizeof(wm_entry));
    data->count = 0;

    for (int64_t i = 0; i < old_cap; i++)
    {
        if (old_entries[i].occupied)
        {
            int64_t slot = wm_find_slot(data, rt_string_cstr(old_entries[i].key));
            data->entries[slot].key = old_entries[i].key;
            data->entries[slot].value = old_entries[i].value;
            data->entries[slot].occupied = 1;
            data->count++;
        }
    }
    free(old_entries);
}

static void weakmap_finalizer(void *obj)
{
    rt_weakmap_data *data = (rt_weakmap_data *)obj;
    if (data->entries)
    {
        for (int64_t i = 0; i < data->capacity; i++)
        {
            if (data->entries[i].occupied && data->entries[i].key)
                rt_string_unref(data->entries[i].key);
        }
        free(data->entries);
        data->entries = NULL;
    }
}

// --- Public API ---

void *rt_weakmap_new(void)
{
    void *obj = rt_obj_new_i64(0, sizeof(rt_weakmap_data));
    rt_weakmap_data *data = (rt_weakmap_data *)obj;
    data->entries = (wm_entry *)calloc(WM_INITIAL_CAP, sizeof(wm_entry));
    data->capacity = WM_INITIAL_CAP;
    data->count = 0;
    rt_obj_set_finalizer(obj, weakmap_finalizer);
    return obj;
}

int64_t rt_weakmap_len(void *map)
{
    if (!map)
        return 0;
    return ((rt_weakmap_data *)map)->count;
}

int8_t rt_weakmap_is_empty(void *map)
{
    return rt_weakmap_len(map) == 0 ? 1 : 0;
}

void rt_weakmap_set(void *map, rt_string key, void *value)
{
    if (!map || !key)
        return;
    rt_weakmap_data *data = (rt_weakmap_data *)map;

    // Grow at 70% load
    if (data->count * 10 >= data->capacity * 7)
        wm_grow(data);

    const char *key_cstr = rt_string_cstr(key);
    int64_t slot = wm_find_slot(data, key_cstr);
    if (slot < 0)
        return;

    if (data->entries[slot].occupied)
    {
        // Update existing - don't retain/release value (weak)
        data->entries[slot].value = value;
    }
    else
    {
        // New entry
        data->entries[slot].key = key;
        rt_obj_retain_maybe(key);
        data->entries[slot].value = value; // NOT retained
        data->entries[slot].occupied = 1;
        data->count++;
    }
}

void *rt_weakmap_get(void *map, rt_string key)
{
    if (!map || !key)
        return NULL;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    int64_t slot = wm_find_slot(data, rt_string_cstr(key));
    if (slot < 0 || !data->entries[slot].occupied)
        return NULL;
    return data->entries[slot].value;
}

int8_t rt_weakmap_has(void *map, rt_string key)
{
    if (!map || !key)
        return 0;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    int64_t slot = wm_find_slot(data, rt_string_cstr(key));
    return (slot >= 0 && data->entries[slot].occupied) ? 1 : 0;
}

int8_t rt_weakmap_remove(void *map, rt_string key)
{
    if (!map || !key)
        return 0;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    int64_t slot = wm_find_slot(data, rt_string_cstr(key));
    if (slot < 0 || !data->entries[slot].occupied)
        return 0;

    rt_string_unref(data->entries[slot].key);
    data->entries[slot].key = NULL;
    data->entries[slot].value = NULL;
    data->entries[slot].occupied = 0;
    data->count--;

    // Rehash subsequent entries to maintain open addressing
    int64_t next = (slot + 1) % data->capacity;
    while (data->entries[next].occupied)
    {
        wm_entry tmp = data->entries[next];
        data->entries[next].occupied = 0;
        data->count--;
        int64_t new_slot = wm_find_slot(data, rt_string_cstr(tmp.key));
        data->entries[new_slot] = tmp;
        data->count++;
        next = (next + 1) % data->capacity;
    }

    return 1;
}

void *rt_weakmap_keys(void *map)
{
    void *seq = rt_seq_new();
    if (!map)
        return seq;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    for (int64_t i = 0; i < data->capacity; i++)
    {
        if (data->entries[i].occupied)
            rt_seq_push(seq, data->entries[i].key);
    }
    return seq;
}

void rt_weakmap_clear(void *map)
{
    if (!map)
        return;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    for (int64_t i = 0; i < data->capacity; i++)
    {
        if (data->entries[i].occupied)
        {
            rt_string_unref(data->entries[i].key);
            data->entries[i].key = NULL;
            data->entries[i].value = NULL;
            data->entries[i].occupied = 0;
        }
    }
    data->count = 0;
}

int64_t rt_weakmap_compact(void *map)
{
    if (!map)
        return 0;
    rt_weakmap_data *data = (rt_weakmap_data *)map;
    int64_t removed = 0;

    // Collect entries with NULL values
    for (int64_t i = 0; i < data->capacity; i++)
    {
        if (data->entries[i].occupied && data->entries[i].value == NULL)
        {
            rt_string_unref(data->entries[i].key);
            data->entries[i].key = NULL;
            data->entries[i].occupied = 0;
            data->count--;
            removed++;
        }
    }
    return removed;
}
