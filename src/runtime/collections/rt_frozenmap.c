//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_frozenmap.c
// Purpose: Implements an immutable string-keyed map (FrozenMap) built once from
//   a Seq of alternating key/value pairs or from a parallel keys/values Seq.
//   After construction the map cannot be modified; all mutating operations are
//   absent from the API. Uses open-addressing with FNV-1a hashing for O(1)
//   average-case lookup.
//
// Key invariants:
//   - Open-addressing hash table; load factor is kept below 50% by sizing the
//     slot array to 2× the number of entries at construction time.
//   - Slot key == NULL indicates an empty slot (tombstones are not used since
//     the map is immutable after build).
//   - FNV-1a hash over the raw string bytes; linear probing on collision.
//   - Keys are stored as retained rt_string references (not copied); the FrozenMap
//     keeps a reference to prevent GC collection of key strings.
//   - Values are stored as raw void* pointers (not retained); callers must
//     ensure value lifetime exceeds the FrozenMap lifetime.
//   - Not thread-safe for construction; safe for concurrent read-only access
//     after construction completes.
//
// Ownership/Lifetime:
//   - FrozenMap objects are GC-managed (rt_obj_new_i64). The slots array is
//     freed by the GC finalizer (frozenmap_finalizer).
//
// Links: src/runtime/collections/rt_frozenmap.h (public API),
//        src/runtime/collections/rt_map.h (mutable map counterpart)
//
//===----------------------------------------------------------------------===//

#include "rt_frozenmap.h"

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

// --- Helper: extract string from seq element (may be boxed) ---

static rt_string fm_extract_str(void *elem)
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
    void *value;
} fm_slot;

typedef struct
{
    void *vptr;
    int64_t count;
    int64_t capacity;
    fm_slot *slots;
} rt_frozenmap_impl;

// --- FNV-1a hash ---

static uint64_t fm_hash(const char *data, int64_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (int64_t i = 0; i < len; i++)
    {
        h ^= (uint8_t)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t fm_str_hash(rt_string s)
{
    const char *cstr = rt_string_cstr(s);
    return fm_hash(cstr, (int64_t)strlen(cstr));
}

// --- Internal helpers ---

static void fm_finalizer(void *obj)
{
    rt_frozenmap_impl *fm = (rt_frozenmap_impl *)obj;
    if (fm->slots)
    {
        for (int64_t i = 0; i < fm->capacity; i++)
        {
            if (fm->slots[i].key)
            {
                rt_string_unref(fm->slots[i].key);
                rt_obj_release_check0(fm->slots[i].value);
            }
        }
        free(fm->slots);
        fm->slots = NULL;
    }
}

static int64_t fm_next_pow2(int64_t n)
{
    int64_t p = 16;
    while (p < n)
        p *= 2;
    return p;
}

static rt_frozenmap_impl *fm_alloc(int64_t count)
{
    int64_t cap = fm_next_pow2(count < 4 ? 8 : count * 2);
    rt_frozenmap_impl *fm = (rt_frozenmap_impl *)rt_obj_new_i64(0, sizeof(rt_frozenmap_impl));
    fm->count = 0;
    fm->capacity = cap;
    fm->slots = (fm_slot *)calloc((size_t)cap, sizeof(fm_slot));
    rt_obj_set_finalizer(fm, fm_finalizer);
    return fm;
}

// Insert or update. Returns 1 if new entry, 0 if updated.
static int8_t fm_insert(rt_frozenmap_impl *fm, rt_string key, void *value)
{
    uint64_t h = fm_str_hash(key);
    int64_t mask = fm->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);
    const char *key_cstr = rt_string_cstr(key);

    for (int64_t i = 0; i < fm->capacity; i++)
    {
        int64_t slot = (idx + i) & mask;
        if (!fm->slots[slot].key)
        {
            fm->slots[slot].key = key;
            rt_obj_retain_maybe(key);
            fm->slots[slot].value = value;
            rt_obj_retain_maybe(value);
            fm->count++;
            return 1;
        }
        if (strcmp(rt_string_cstr(fm->slots[slot].key), key_cstr) == 0)
        {
            // Update value (last writer wins)
            rt_obj_release_check0(fm->slots[slot].value);
            fm->slots[slot].value = value;
            rt_obj_retain_maybe(value);
            return 0;
        }
    }
    return 0;
}

static fm_slot *fm_find(rt_frozenmap_impl *fm, rt_string key)
{
    if (!fm || fm->count == 0)
        return NULL;
    uint64_t h = fm_str_hash(key);
    int64_t mask = fm->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);
    const char *key_cstr = rt_string_cstr(key);

    for (int64_t i = 0; i < fm->capacity; i++)
    {
        int64_t slot = (idx + i) & mask;
        if (!fm->slots[slot].key)
            return NULL;
        if (strcmp(rt_string_cstr(fm->slots[slot].key), key_cstr) == 0)
            return &fm->slots[slot];
    }
    return NULL;
}

// --- Public API ---

void *rt_frozenmap_from_seqs(void *keys, void *values)
{
    if (!keys || !values)
        return (void *)fm_alloc(0);

    int64_t nk = rt_seq_len(keys);
    int64_t nv = rt_seq_len(values);
    int64_t n = nk < nv ? nk : nv;

    rt_frozenmap_impl *fm = fm_alloc(n);

    for (int64_t i = 0; i < n; i++)
    {
        rt_string k = fm_extract_str(rt_seq_get(keys, i));
        void *v = rt_seq_get(values, i);
        if (k)
            fm_insert(fm, k, v);
    }
    return (void *)fm;
}

void *rt_frozenmap_empty(void)
{
    return (void *)fm_alloc(0);
}

int64_t rt_frozenmap_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_frozenmap_impl *)obj)->count;
}

int8_t rt_frozenmap_is_empty(void *obj)
{
    return rt_frozenmap_len(obj) == 0 ? 1 : 0;
}

void *rt_frozenmap_get(void *obj, rt_string key)
{
    if (!obj || !key)
        return NULL;
    fm_slot *s = fm_find((rt_frozenmap_impl *)obj, key);
    return s ? s->value : NULL;
}

int8_t rt_frozenmap_has(void *obj, rt_string key)
{
    if (!obj || !key)
        return 0;
    return fm_find((rt_frozenmap_impl *)obj, key) != NULL ? 1 : 0;
}

void *rt_frozenmap_keys(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;

    rt_frozenmap_impl *fm = (rt_frozenmap_impl *)obj;
    for (int64_t i = 0; i < fm->capacity; i++)
    {
        if (fm->slots[i].key)
            rt_seq_push(seq, fm->slots[i].key);
    }
    return seq;
}

void *rt_frozenmap_values(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;

    rt_frozenmap_impl *fm = (rt_frozenmap_impl *)obj;
    for (int64_t i = 0; i < fm->capacity; i++)
    {
        if (fm->slots[i].key)
            rt_seq_push(seq, fm->slots[i].value);
    }
    return seq;
}

void *rt_frozenmap_get_or(void *obj, rt_string key, void *default_value)
{
    if (!obj || !key)
        return default_value;
    fm_slot *s = fm_find((rt_frozenmap_impl *)obj, key);
    return s ? s->value : default_value;
}

void *rt_frozenmap_merge(void *obj, void *other)
{
    int64_t la = rt_frozenmap_len(obj);
    int64_t lb = rt_frozenmap_len(other);
    rt_frozenmap_impl *fm = fm_alloc(la + lb);

    // Insert from first map
    if (obj)
    {
        rt_frozenmap_impl *a = (rt_frozenmap_impl *)obj;
        for (int64_t i = 0; i < a->capacity; i++)
        {
            if (a->slots[i].key)
                fm_insert(fm, a->slots[i].key, a->slots[i].value);
        }
    }
    // Insert from second map (overwrites on conflict)
    if (other)
    {
        rt_frozenmap_impl *b = (rt_frozenmap_impl *)other;
        for (int64_t i = 0; i < b->capacity; i++)
        {
            if (b->slots[i].key)
                fm_insert(fm, b->slots[i].key, b->slots[i].value);
        }
    }
    return (void *)fm;
}

int8_t rt_frozenmap_equals(void *obj, void *other)
{
    int64_t la = rt_frozenmap_len(obj);
    int64_t lb = rt_frozenmap_len(other);
    if (la != lb)
        return 0;

    if (!obj)
        return 1; // both empty

    rt_frozenmap_impl *a = (rt_frozenmap_impl *)obj;
    rt_frozenmap_impl *b = (rt_frozenmap_impl *)other;

    for (int64_t i = 0; i < a->capacity; i++)
    {
        if (a->slots[i].key)
        {
            fm_slot *bs = fm_find(b, a->slots[i].key);
            if (!bs || bs->value != a->slots[i].value)
                return 0;
        }
    }
    return 1;
}
