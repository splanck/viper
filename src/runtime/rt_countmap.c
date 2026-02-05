//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_countmap.c
// Purpose: Implement a frequency counting map (string key -> i64 count) using
//          a hash table. O(1) average-case operations.
//
//===----------------------------------------------------------------------===//

#include "rt_countmap.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#define CM_INITIAL_CAPACITY 16
#define CM_LOAD_FACTOR_NUM 3
#define CM_LOAD_FACTOR_DEN 4
#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

typedef struct rt_cm_entry
{
    char *key;
    size_t key_len;
    int64_t count;
    struct rt_cm_entry *next;
} rt_cm_entry;

typedef struct rt_countmap_impl
{
    void **vptr;
    rt_cm_entry **buckets;
    size_t capacity;
    size_t count;       // distinct keys
    int64_t total;      // sum of all counts
} rt_countmap_impl;

static uint64_t fnv1a(const char *data, size_t len)
{
    uint64_t hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= (uint8_t)data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static const char *get_str_data(rt_string s, size_t *out_len)
{
    const char *cstr = rt_string_cstr(s);
    if (!cstr)
    {
        *out_len = 0;
        return "";
    }
    *out_len = strlen(cstr);
    return cstr;
}

static rt_cm_entry *find_entry(rt_cm_entry *head, const char *key, size_t key_len)
{
    for (rt_cm_entry *e = head; e; e = e->next)
    {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

static void free_entry(rt_cm_entry *entry)
{
    if (!entry)
        return;
    free(entry->key);
    free(entry);
}

static void countmap_finalizer(void *obj)
{
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;
    if (!cm)
        return;

    for (size_t i = 0; i < cm->capacity; ++i)
    {
        rt_cm_entry *e = cm->buckets[i];
        while (e)
        {
            rt_cm_entry *next = e->next;
            free_entry(e);
            e = next;
        }
    }
    free(cm->buckets);
}

static void resize(rt_countmap_impl *cm)
{
    size_t new_cap = cm->capacity * 2;
    rt_cm_entry **new_buckets = (rt_cm_entry **)calloc(new_cap, sizeof(rt_cm_entry *));
    if (!new_buckets)
        return;

    for (size_t i = 0; i < cm->capacity; ++i)
    {
        rt_cm_entry *e = cm->buckets[i];
        while (e)
        {
            rt_cm_entry *next = e->next;
            uint64_t h = fnv1a(e->key, e->key_len);
            size_t idx = (size_t)(h % new_cap);
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }

    free(cm->buckets);
    cm->buckets = new_buckets;
    cm->capacity = new_cap;
}

void *rt_countmap_new(void)
{
    rt_countmap_impl *cm = (rt_countmap_impl *)rt_obj_new_i64(0, sizeof(rt_countmap_impl));
    if (!cm)
        return NULL;

    cm->capacity = CM_INITIAL_CAPACITY;
    cm->count = 0;
    cm->total = 0;
    cm->buckets = (rt_cm_entry **)calloc(CM_INITIAL_CAPACITY, sizeof(rt_cm_entry *));
    if (!cm->buckets)
        return NULL;

    rt_obj_set_finalizer(cm, countmap_finalizer);
    return cm;
}

int64_t rt_countmap_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_countmap_impl *)obj)->count;
}

int8_t rt_countmap_is_empty(void *obj)
{
    return rt_countmap_len(obj) == 0 ? 1 : 0;
}

int64_t rt_countmap_inc(void *obj, rt_string key)
{
    return rt_countmap_inc_by(obj, key, 1);
}

int64_t rt_countmap_inc_by(void *obj, rt_string key, int64_t n)
{
    if (!obj || n <= 0)
        return 0;
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = fnv1a(kdata, klen);
    size_t idx = (size_t)(h % cm->capacity);

    rt_cm_entry *e = find_entry(cm->buckets[idx], kdata, klen);
    if (e)
    {
        e->count += n;
        cm->total += n;
        return e->count;
    }

    // New entry
    if (cm->count * CM_LOAD_FACTOR_DEN >= cm->capacity * CM_LOAD_FACTOR_NUM)
    {
        resize(cm);
        idx = (size_t)(h % cm->capacity);
    }

    e = (rt_cm_entry *)malloc(sizeof(rt_cm_entry));
    if (!e)
        return 0;
    e->key = (char *)malloc(klen + 1);
    if (!e->key)
    {
        free(e);
        return 0;
    }
    memcpy(e->key, kdata, klen);
    e->key[klen] = '\0';
    e->key_len = klen;
    e->count = n;
    e->next = cm->buckets[idx];
    cm->buckets[idx] = e;
    cm->count++;
    cm->total += n;
    return e->count;
}

int64_t rt_countmap_dec(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = fnv1a(kdata, klen);
    size_t idx = (size_t)(h % cm->capacity);

    rt_cm_entry **pp = &cm->buckets[idx];
    while (*pp)
    {
        rt_cm_entry *e = *pp;
        if (e->key_len == klen && memcmp(e->key, kdata, klen) == 0)
        {
            e->count--;
            cm->total--;
            if (e->count <= 0)
            {
                *pp = e->next;
                free_entry(e);
                cm->count--;
                return 0;
            }
            return e->count;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

int64_t rt_countmap_get(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = fnv1a(kdata, klen);
    size_t idx = (size_t)(h % cm->capacity);

    rt_cm_entry *e = find_entry(cm->buckets[idx], kdata, klen);
    return e ? e->count : 0;
}

void rt_countmap_set(void *obj, rt_string key, int64_t count)
{
    if (!obj)
        return;
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = fnv1a(kdata, klen);
    size_t idx = (size_t)(h % cm->capacity);

    // Remove if count <= 0
    if (count <= 0)
    {
        rt_cm_entry **pp = &cm->buckets[idx];
        while (*pp)
        {
            rt_cm_entry *e = *pp;
            if (e->key_len == klen && memcmp(e->key, kdata, klen) == 0)
            {
                *pp = e->next;
                cm->total -= e->count;
                free_entry(e);
                cm->count--;
                return;
            }
            pp = &(*pp)->next;
        }
        return;
    }

    rt_cm_entry *e = find_entry(cm->buckets[idx], kdata, klen);
    if (e)
    {
        cm->total += (count - e->count);
        e->count = count;
        return;
    }

    // New entry
    if (cm->count * CM_LOAD_FACTOR_DEN >= cm->capacity * CM_LOAD_FACTOR_NUM)
    {
        resize(cm);
        idx = (size_t)(h % cm->capacity);
    }

    e = (rt_cm_entry *)malloc(sizeof(rt_cm_entry));
    if (!e)
        return;
    e->key = (char *)malloc(klen + 1);
    if (!e->key)
    {
        free(e);
        return;
    }
    memcpy(e->key, kdata, klen);
    e->key[klen] = '\0';
    e->key_len = klen;
    e->count = count;
    e->next = cm->buckets[idx];
    cm->buckets[idx] = e;
    cm->count++;
    cm->total += count;
}

int8_t rt_countmap_has(void *obj, rt_string key)
{
    return rt_countmap_get(obj, key) > 0 ? 1 : 0;
}

int64_t rt_countmap_total(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_countmap_impl *)obj)->total;
}

void *rt_countmap_keys(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;

    for (size_t i = 0; i < cm->capacity; ++i)
    {
        for (rt_cm_entry *e = cm->buckets[i]; e; e = e->next)
        {
            rt_string k = rt_string_from_bytes(e->key, e->key_len);
            rt_seq_push(seq, (void *)k);
        }
    }
    return seq;
}

// Comparison for sorting entries by count descending
static int cmp_entries_desc(const void *a, const void *b)
{
    const rt_cm_entry *ea = *(const rt_cm_entry *const *)a;
    const rt_cm_entry *eb = *(const rt_cm_entry *const *)b;
    if (ea->count > eb->count)
        return -1;
    if (ea->count < eb->count)
        return 1;
    return 0;
}

void *rt_countmap_most_common(void *obj, int64_t n)
{
    void *seq = rt_seq_new();
    if (!obj || n <= 0)
        return seq;
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;

    if (cm->count == 0)
        return seq;

    // Collect all entries into a flat array
    rt_cm_entry **entries = (rt_cm_entry **)malloc(cm->count * sizeof(rt_cm_entry *));
    if (!entries)
        return seq;

    size_t idx = 0;
    for (size_t i = 0; i < cm->capacity; ++i)
    {
        for (rt_cm_entry *e = cm->buckets[i]; e; e = e->next)
        {
            entries[idx++] = e;
        }
    }

    // Sort by count descending
    qsort(entries, cm->count, sizeof(rt_cm_entry *), cmp_entries_desc);

    // Take top N
    size_t limit = (size_t)n;
    if (limit > cm->count)
        limit = cm->count;

    for (size_t i = 0; i < limit; ++i)
    {
        rt_string k = rt_string_from_bytes(entries[i]->key, entries[i]->key_len);
        rt_seq_push(seq, (void *)k);
    }

    free(entries);
    return seq;
}

int8_t rt_countmap_remove(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = fnv1a(kdata, klen);
    size_t idx = (size_t)(h % cm->capacity);

    rt_cm_entry **pp = &cm->buckets[idx];
    while (*pp)
    {
        rt_cm_entry *e = *pp;
        if (e->key_len == klen && memcmp(e->key, kdata, klen) == 0)
        {
            *pp = e->next;
            cm->total -= e->count;
            free_entry(e);
            cm->count--;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

void rt_countmap_clear(void *obj)
{
    if (!obj)
        return;
    rt_countmap_impl *cm = (rt_countmap_impl *)obj;

    for (size_t i = 0; i < cm->capacity; ++i)
    {
        rt_cm_entry *e = cm->buckets[i];
        while (e)
        {
            rt_cm_entry *next = e->next;
            free_entry(e);
            e = next;
        }
        cm->buckets[i] = NULL;
    }
    cm->count = 0;
    cm->total = 0;
}
