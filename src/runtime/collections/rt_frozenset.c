//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_frozenset.h"

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

// --- Helper: extract string from seq element (may be boxed) ---

static rt_string fs_extract_str(void *elem)
{
    if (!elem)
        return NULL;
    // Check if the element is a raw rt_string by inspecting the magic field.
    // rt_string_impl starts with magic = RT_STRING_MAGIC; boxed values do not.
    rt_string s = (rt_string)elem;
    if (s->magic == RT_STRING_MAGIC)
        return s;
    // Not a raw string -- assume boxed value and unbox.
    return rt_unbox_str(elem);
}

// --- Hash table entry (open addressing) ---

typedef struct
{
    rt_string key; // NULL = empty slot
} fs_slot;

typedef struct
{
    void *vptr;
    int64_t count;
    int64_t capacity;
    fs_slot *slots;
} rt_frozenset_impl;

// --- FNV-1a hash ---

static uint64_t fs_hash(const char *data, int64_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (int64_t i = 0; i < len; i++)
    {
        h ^= (uint8_t)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t fs_str_hash(rt_string s)
{
    const char *cstr = rt_string_cstr(s);
    return fs_hash(cstr, (int64_t)strlen(cstr));
}

// --- Internal helpers ---

static void fs_finalizer(void *obj)
{
    rt_frozenset_impl *fs = (rt_frozenset_impl *)obj;
    if (fs->slots)
    {
        for (int64_t i = 0; i < fs->capacity; i++)
        {
            if (fs->slots[i].key)
                rt_string_unref(fs->slots[i].key);
        }
        free(fs->slots);
        fs->slots = NULL;
    }
}

static int64_t fs_next_pow2(int64_t n)
{
    int64_t p = 16;
    while (p < n)
        p *= 2;
    return p;
}

static rt_frozenset_impl *fs_alloc(int64_t count)
{
    // Use ~50% load factor for good probe performance
    int64_t cap = fs_next_pow2(count < 4 ? 8 : count * 2);
    rt_frozenset_impl *fs = (rt_frozenset_impl *)rt_obj_new_i64(0, sizeof(rt_frozenset_impl));
    fs->count = 0;
    fs->capacity = cap;
    fs->slots = (fs_slot *)calloc((size_t)cap, sizeof(fs_slot));
    rt_obj_set_finalizer(fs, fs_finalizer);
    return fs;
}

static int8_t fs_insert(rt_frozenset_impl *fs, rt_string key)
{
    uint64_t h = fs_str_hash(key);
    int64_t mask = fs->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);
    const char *key_cstr = rt_string_cstr(key);

    for (int64_t i = 0; i < fs->capacity; i++)
    {
        int64_t slot = (idx + i) & mask;
        if (!fs->slots[slot].key)
        {
            fs->slots[slot].key = key;
            rt_obj_retain_maybe(key);
            fs->count++;
            return 1;
        }
        if (strcmp(rt_string_cstr(fs->slots[slot].key), key_cstr) == 0)
            return 0; // duplicate
    }
    return 0;
}

static int8_t fs_contains(rt_frozenset_impl *fs, rt_string key)
{
    if (!fs || fs->count == 0)
        return 0;
    uint64_t h = fs_str_hash(key);
    int64_t mask = fs->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);
    const char *key_cstr = rt_string_cstr(key);

    for (int64_t i = 0; i < fs->capacity; i++)
    {
        int64_t slot = (idx + i) & mask;
        if (!fs->slots[slot].key)
            return 0;
        if (strcmp(rt_string_cstr(fs->slots[slot].key), key_cstr) == 0)
            return 1;
    }
    return 0;
}

// --- Public API ---

void *rt_frozenset_from_seq(void *items)
{
    if (!items)
        return (void *)fs_alloc(0);

    int64_t n = rt_seq_len(items);
    rt_frozenset_impl *fs = fs_alloc(n);

    for (int64_t i = 0; i < n; i++)
    {
        rt_string elem = fs_extract_str(rt_seq_get(items, i));
        if (elem)
            fs_insert(fs, elem);
    }
    return (void *)fs;
}

void *rt_frozenset_empty(void)
{
    return (void *)fs_alloc(0);
}

int64_t rt_frozenset_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_frozenset_impl *)obj)->count;
}

int8_t rt_frozenset_is_empty(void *obj)
{
    return rt_frozenset_len(obj) == 0 ? 1 : 0;
}

int8_t rt_frozenset_has(void *obj, rt_string elem)
{
    if (!obj || !elem)
        return 0;
    return fs_contains((rt_frozenset_impl *)obj, elem);
}

void *rt_frozenset_items(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;

    rt_frozenset_impl *fs = (rt_frozenset_impl *)obj;
    for (int64_t i = 0; i < fs->capacity; i++)
    {
        if (fs->slots[i].key)
            rt_seq_push(seq, fs->slots[i].key);
    }
    return seq;
}

void *rt_frozenset_union(void *obj, void *other)
{
    // Collect all elements from both sets
    void *seq = rt_seq_new();

    if (obj)
    {
        rt_frozenset_impl *a = (rt_frozenset_impl *)obj;
        for (int64_t i = 0; i < a->capacity; i++)
        {
            if (a->slots[i].key)
                rt_seq_push(seq, a->slots[i].key);
        }
    }
    if (other)
    {
        rt_frozenset_impl *b = (rt_frozenset_impl *)other;
        for (int64_t i = 0; i < b->capacity; i++)
        {
            if (b->slots[i].key)
                rt_seq_push(seq, b->slots[i].key);
        }
    }

    void *result = rt_frozenset_from_seq(seq);
    return result;
}

void *rt_frozenset_intersect(void *obj, void *other)
{
    void *seq = rt_seq_new();
    if (!obj || !other)
        return rt_frozenset_from_seq(seq);

    rt_frozenset_impl *a = (rt_frozenset_impl *)obj;
    rt_frozenset_impl *b = (rt_frozenset_impl *)other;

    for (int64_t i = 0; i < a->capacity; i++)
    {
        if (a->slots[i].key && fs_contains(b, a->slots[i].key))
            rt_seq_push(seq, a->slots[i].key);
    }

    return rt_frozenset_from_seq(seq);
}

void *rt_frozenset_diff(void *obj, void *other)
{
    void *seq = rt_seq_new();
    if (!obj)
        return rt_frozenset_from_seq(seq);

    rt_frozenset_impl *a = (rt_frozenset_impl *)obj;

    for (int64_t i = 0; i < a->capacity; i++)
    {
        if (a->slots[i].key)
        {
            if (!other || !fs_contains((rt_frozenset_impl *)other, a->slots[i].key))
                rt_seq_push(seq, a->slots[i].key);
        }
    }

    return rt_frozenset_from_seq(seq);
}

int8_t rt_frozenset_is_subset(void *obj, void *other)
{
    if (!obj)
        return 1; // empty set is subset of everything
    if (!other)
        return ((rt_frozenset_impl *)obj)->count == 0 ? 1 : 0;

    rt_frozenset_impl *a = (rt_frozenset_impl *)obj;
    rt_frozenset_impl *b = (rt_frozenset_impl *)other;

    for (int64_t i = 0; i < a->capacity; i++)
    {
        if (a->slots[i].key && !fs_contains(b, a->slots[i].key))
            return 0;
    }
    return 1;
}

int8_t rt_frozenset_equals(void *obj, void *other)
{
    int64_t la = rt_frozenset_len(obj);
    int64_t lb = rt_frozenset_len(other);
    if (la != lb)
        return 0;
    return rt_frozenset_is_subset(obj, other);
}
