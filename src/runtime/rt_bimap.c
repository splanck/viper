//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_bimap.c
// Purpose: Implement a bidirectional string-to-string map using two hash tables
//          (forward: key->value, inverse: value->key). O(1) average-case lookup
//          in both directions.
//
//===----------------------------------------------------------------------===//

#include "rt_bimap.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#define BM_INITIAL_CAPACITY 16
#define BM_LOAD_FACTOR_NUM 3
#define BM_LOAD_FACTOR_DEN 4
#include "rt_hash_util.h"

typedef struct rt_bm_entry
{
    char *key;
    size_t key_len;
    char *value;
    size_t value_len;
    struct rt_bm_entry *next;
} rt_bm_entry;

typedef struct rt_bimap_impl
{
    void **vptr;
    rt_bm_entry **fwd_buckets; // key -> entry
    rt_bm_entry **inv_buckets; // value -> entry (same entry objects, different chain)
    size_t fwd_capacity;
    size_t inv_capacity;
    size_t count;

    // Separate chains for inverse lookups
    struct rt_bm_inv_link
    {
        rt_bm_entry *entry;
        struct rt_bm_inv_link *next;
    } **inv_chains;
} rt_bimap_impl;

// Inverse lookup chain node
typedef struct rt_bm_inv_link rt_bm_inv_link;


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

static rt_bm_entry *find_fwd(rt_bm_entry *head, const char *key, size_t key_len)
{
    for (rt_bm_entry *e = head; e; e = e->next)
    {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

static rt_bm_inv_link *find_inv(rt_bm_inv_link *head, const char *val, size_t val_len)
{
    for (rt_bm_inv_link *l = head; l; l = l->next)
    {
        if (l->entry->value_len == val_len && memcmp(l->entry->value, val, val_len) == 0)
            return l;
    }
    return NULL;
}

static void remove_inv_link(rt_bimap_impl *bm, const char *val, size_t val_len)
{
    uint64_t h = rt_fnv1a(val, val_len);
    size_t idx = (size_t)(h % bm->inv_capacity);
    rt_bm_inv_link **pp = &bm->inv_chains[idx];
    while (*pp)
    {
        if ((*pp)->entry->value_len == val_len && memcmp((*pp)->entry->value, val, val_len) == 0)
        {
            rt_bm_inv_link *old = *pp;
            *pp = old->next;
            free(old);
            return;
        }
        pp = &(*pp)->next;
    }
}

static void add_inv_link(rt_bimap_impl *bm, rt_bm_entry *entry)
{
    uint64_t h = rt_fnv1a(entry->value, entry->value_len);
    size_t idx = (size_t)(h % bm->inv_capacity);
    rt_bm_inv_link *link = (rt_bm_inv_link *)malloc(sizeof(rt_bm_inv_link));
    if (!link)
        return;
    link->entry = entry;
    link->next = bm->inv_chains[idx];
    bm->inv_chains[idx] = link;
}

static void free_entry(rt_bm_entry *entry)
{
    if (!entry)
        return;
    free(entry->key);
    free(entry->value);
    free(entry);
}

static void bimap_finalizer(void *obj)
{
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;
    if (!bm)
        return;

    // Free forward entries
    for (size_t i = 0; i < bm->fwd_capacity; ++i)
    {
        rt_bm_entry *e = bm->fwd_buckets[i];
        while (e)
        {
            rt_bm_entry *next = e->next;
            free_entry(e);
            e = next;
        }
    }
    free(bm->fwd_buckets);

    // Free inverse chains
    for (size_t i = 0; i < bm->inv_capacity; ++i)
    {
        rt_bm_inv_link *l = bm->inv_chains[i];
        while (l)
        {
            rt_bm_inv_link *next = l->next;
            free(l);
            l = next;
        }
    }
    free(bm->inv_chains);
}

static void resize_fwd(rt_bimap_impl *bm)
{
    size_t new_cap = bm->fwd_capacity * 2;
    rt_bm_entry **new_buckets = (rt_bm_entry **)calloc(new_cap, sizeof(rt_bm_entry *));
    if (!new_buckets)
        return;

    for (size_t i = 0; i < bm->fwd_capacity; ++i)
    {
        rt_bm_entry *e = bm->fwd_buckets[i];
        while (e)
        {
            rt_bm_entry *next = e->next;
            uint64_t h = rt_fnv1a(e->key, e->key_len);
            size_t idx = (size_t)(h % new_cap);
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }

    free(bm->fwd_buckets);
    bm->fwd_buckets = new_buckets;
    bm->fwd_capacity = new_cap;
}

static void resize_inv(rt_bimap_impl *bm)
{
    size_t new_cap = bm->inv_capacity * 2;
    rt_bm_inv_link **new_chains = (rt_bm_inv_link **)calloc(new_cap, sizeof(rt_bm_inv_link *));
    if (!new_chains)
        return;

    for (size_t i = 0; i < bm->inv_capacity; ++i)
    {
        rt_bm_inv_link *l = bm->inv_chains[i];
        while (l)
        {
            rt_bm_inv_link *next = l->next;
            uint64_t h = rt_fnv1a(l->entry->value, l->entry->value_len);
            size_t idx = (size_t)(h % new_cap);
            l->next = new_chains[idx];
            new_chains[idx] = l;
            l = next;
        }
    }

    free(bm->inv_chains);
    bm->inv_chains = new_chains;
    bm->inv_capacity = new_cap;
}

void *rt_bimap_new(void)
{
    rt_bimap_impl *bm = (rt_bimap_impl *)rt_obj_new_i64(0, sizeof(rt_bimap_impl));
    if (!bm)
        return NULL;

    bm->fwd_capacity = BM_INITIAL_CAPACITY;
    bm->inv_capacity = BM_INITIAL_CAPACITY;
    bm->count = 0;
    bm->fwd_buckets = (rt_bm_entry **)calloc(BM_INITIAL_CAPACITY, sizeof(rt_bm_entry *));
    bm->inv_chains = (rt_bm_inv_link **)calloc(BM_INITIAL_CAPACITY, sizeof(rt_bm_inv_link *));
    if (!bm->fwd_buckets || !bm->inv_chains)
    {
        free(bm->fwd_buckets);
        free(bm->inv_chains);
        return NULL;
    }

    rt_obj_set_finalizer(bm, bimap_finalizer);
    return bm;
}

int64_t rt_bimap_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_bimap_impl *)obj)->count;
}

int8_t rt_bimap_is_empty(void *obj)
{
    return rt_bimap_len(obj) == 0 ? 1 : 0;
}

void rt_bimap_put(void *obj, rt_string key, rt_string value)
{
    if (!obj)
        return;
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    size_t klen, vlen;
    const char *kdata = get_str_data(key, &klen);
    const char *vdata = get_str_data(value, &vlen);

    // Remove existing mapping for this key (if any)
    rt_bimap_remove_by_key(obj, key);

    // Remove existing mapping for this value (if any)
    rt_bimap_remove_by_value(obj, value);

    // Check load factor on forward table
    if (bm->count * BM_LOAD_FACTOR_DEN >= bm->fwd_capacity * BM_LOAD_FACTOR_NUM)
        resize_fwd(bm);
    if (bm->count * BM_LOAD_FACTOR_DEN >= bm->inv_capacity * BM_LOAD_FACTOR_NUM)
        resize_inv(bm);

    // Create entry
    rt_bm_entry *entry = (rt_bm_entry *)malloc(sizeof(rt_bm_entry));
    if (!entry)
        return;
    entry->key = (char *)malloc(klen + 1);
    entry->value = (char *)malloc(vlen + 1);
    if (!entry->key || !entry->value)
    {
        free(entry->key);
        free(entry->value);
        free(entry);
        return;
    }
    memcpy(entry->key, kdata, klen);
    entry->key[klen] = '\0';
    entry->key_len = klen;
    memcpy(entry->value, vdata, vlen);
    entry->value[vlen] = '\0';
    entry->value_len = vlen;

    // Insert into forward table
    uint64_t fh = rt_fnv1a(kdata, klen);
    size_t fidx = (size_t)(fh % bm->fwd_capacity);
    entry->next = bm->fwd_buckets[fidx];
    bm->fwd_buckets[fidx] = entry;

    // Insert into inverse chain
    add_inv_link(bm, entry);

    bm->count++;
}

rt_string rt_bimap_get_by_key(void *obj, rt_string key)
{
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = rt_fnv1a(kdata, klen);
    size_t idx = (size_t)(h % bm->fwd_capacity);
    rt_bm_entry *e = find_fwd(bm->fwd_buckets[idx], kdata, klen);
    if (!e)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(e->value, e->value_len);
}

rt_string rt_bimap_get_by_value(void *obj, rt_string value)
{
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    size_t vlen;
    const char *vdata = get_str_data(value, &vlen);

    uint64_t h = rt_fnv1a(vdata, vlen);
    size_t idx = (size_t)(h % bm->inv_capacity);
    rt_bm_inv_link *l = find_inv(bm->inv_chains[idx], vdata, vlen);
    if (!l)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(l->entry->key, l->entry->key_len);
}

int8_t rt_bimap_has_key(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = rt_fnv1a(kdata, klen);
    size_t idx = (size_t)(h % bm->fwd_capacity);
    return find_fwd(bm->fwd_buckets[idx], kdata, klen) ? 1 : 0;
}

int8_t rt_bimap_has_value(void *obj, rt_string value)
{
    if (!obj)
        return 0;
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    size_t vlen;
    const char *vdata = get_str_data(value, &vlen);

    uint64_t h = rt_fnv1a(vdata, vlen);
    size_t idx = (size_t)(h % bm->inv_capacity);
    return find_inv(bm->inv_chains[idx], vdata, vlen) ? 1 : 0;
}

int8_t rt_bimap_remove_by_key(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = rt_fnv1a(kdata, klen);
    size_t idx = (size_t)(h % bm->fwd_capacity);

    rt_bm_entry **pp = &bm->fwd_buckets[idx];
    while (*pp)
    {
        rt_bm_entry *e = *pp;
        if (e->key_len == klen && memcmp(e->key, kdata, klen) == 0)
        {
            // Remove from forward chain
            *pp = e->next;
            // Remove from inverse chain
            remove_inv_link(bm, e->value, e->value_len);
            free_entry(e);
            bm->count--;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

int8_t rt_bimap_remove_by_value(void *obj, rt_string value)
{
    if (!obj)
        return 0;
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    size_t vlen;
    const char *vdata = get_str_data(value, &vlen);

    // Find entry via inverse lookup
    uint64_t vh = rt_fnv1a(vdata, vlen);
    size_t vidx = (size_t)(vh % bm->inv_capacity);
    rt_bm_inv_link *l = find_inv(bm->inv_chains[vidx], vdata, vlen);
    if (!l)
        return 0;

    rt_bm_entry *entry = l->entry;

    // Remove from forward chain
    uint64_t fh = rt_fnv1a(entry->key, entry->key_len);
    size_t fidx = (size_t)(fh % bm->fwd_capacity);
    rt_bm_entry **pp = &bm->fwd_buckets[fidx];
    while (*pp)
    {
        if (*pp == entry)
        {
            *pp = entry->next;
            break;
        }
        pp = &(*pp)->next;
    }

    // Remove from inverse chain
    remove_inv_link(bm, entry->value, entry->value_len);
    free_entry(entry);
    bm->count--;
    return 1;
}

void *rt_bimap_keys(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    for (size_t i = 0; i < bm->fwd_capacity; ++i)
    {
        for (rt_bm_entry *e = bm->fwd_buckets[i]; e; e = e->next)
        {
            rt_string k = rt_string_from_bytes(e->key, e->key_len);
            rt_seq_push(seq, (void *)k);
        }
    }
    return seq;
}

void *rt_bimap_values(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    for (size_t i = 0; i < bm->fwd_capacity; ++i)
    {
        for (rt_bm_entry *e = bm->fwd_buckets[i]; e; e = e->next)
        {
            rt_string v = rt_string_from_bytes(e->value, e->value_len);
            rt_seq_push(seq, (void *)v);
        }
    }
    return seq;
}

void rt_bimap_clear(void *obj)
{
    if (!obj)
        return;
    rt_bimap_impl *bm = (rt_bimap_impl *)obj;

    for (size_t i = 0; i < bm->fwd_capacity; ++i)
    {
        rt_bm_entry *e = bm->fwd_buckets[i];
        while (e)
        {
            rt_bm_entry *next = e->next;
            free_entry(e);
            e = next;
        }
        bm->fwd_buckets[i] = NULL;
    }

    for (size_t i = 0; i < bm->inv_capacity; ++i)
    {
        rt_bm_inv_link *l = bm->inv_chains[i];
        while (l)
        {
            rt_bm_inv_link *next = l->next;
            free(l);
            l = next;
        }
        bm->inv_chains[i] = NULL;
    }

    bm->count = 0;
}
