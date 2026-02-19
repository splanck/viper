//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_sparsearray.h"

#include "rt_internal.h"
#include "rt_seq.h"

#include <stdlib.h>
#include <string.h>

// --- Open addressing hash map: int64_t -> void* ---

typedef struct
{
    int64_t key;
    void *value;
    int8_t occupied;
} sa_slot;

typedef struct
{
    void *vptr;
    int64_t count;
    int64_t capacity;
    sa_slot *slots;
} rt_sparse_impl;

// --- Hash function for int64 ---

static uint64_t sa_hash(int64_t key)
{
    uint64_t k = (uint64_t)key;
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// --- Internal helpers ---

static void sa_finalizer(void *obj)
{
    rt_sparse_impl *sa = (rt_sparse_impl *)obj;
    if (sa->slots)
    {
        for (int64_t i = 0; i < sa->capacity; i++)
        {
            if (sa->slots[i].occupied)
                rt_obj_release_check0(sa->slots[i].value);
        }
        free(sa->slots);
        sa->slots = NULL;
    }
}

static int64_t sa_next_pow2(int64_t n)
{
    int64_t p = 16;
    while (p < n)
        p *= 2;
    return p;
}

static void sa_grow(rt_sparse_impl *sa);

static void sa_insert_internal(rt_sparse_impl *sa, int64_t key, void *value)
{
    uint64_t h = sa_hash(key);
    int64_t mask = sa->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);

    for (int64_t i = 0; i < sa->capacity; i++)
    {
        int64_t slot = (idx + i) & mask;
        if (!sa->slots[slot].occupied)
        {
            sa->slots[slot].key = key;
            sa->slots[slot].value = value;
            sa->slots[slot].occupied = 1;
            rt_obj_retain_maybe(value);
            sa->count++;
            return;
        }
        if (sa->slots[slot].key == key)
        {
            // Update value
            rt_obj_release_check0(sa->slots[slot].value);
            sa->slots[slot].value = value;
            rt_obj_retain_maybe(value);
            return;
        }
    }
}

static void sa_grow(rt_sparse_impl *sa)
{
    int64_t old_cap = sa->capacity;
    sa_slot *old_slots = sa->slots;

    sa->capacity = old_cap * 2;
    sa->slots = (sa_slot *)calloc((size_t)sa->capacity, sizeof(sa_slot));
    sa->count = 0;

    for (int64_t i = 0; i < old_cap; i++)
    {
        if (old_slots[i].occupied)
        {
            // Re-insert without retain (we already hold a ref)
            uint64_t h = sa_hash(old_slots[i].key);
            int64_t mask = sa->capacity - 1;
            int64_t idx = (int64_t)(h & (uint64_t)mask);

            for (int64_t j = 0; j < sa->capacity; j++)
            {
                int64_t slot = (idx + j) & mask;
                if (!sa->slots[slot].occupied)
                {
                    sa->slots[slot].key = old_slots[i].key;
                    sa->slots[slot].value = old_slots[i].value;
                    sa->slots[slot].occupied = 1;
                    sa->count++;
                    break;
                }
            }
        }
    }
    free(old_slots);
}

static sa_slot *sa_find(rt_sparse_impl *sa, int64_t key)
{
    if (!sa || sa->count == 0)
        return NULL;
    uint64_t h = sa_hash(key);
    int64_t mask = sa->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);

    for (int64_t i = 0; i < sa->capacity; i++)
    {
        int64_t slot = (idx + i) & mask;
        if (!sa->slots[slot].occupied)
            return NULL;
        if (sa->slots[slot].key == key)
            return &sa->slots[slot];
    }
    return NULL;
}

// --- Public API ---

void *rt_sparse_new(void)
{
    rt_sparse_impl *sa = (rt_sparse_impl *)rt_obj_new_i64(0, sizeof(rt_sparse_impl));
    sa->count = 0;
    sa->capacity = 16;
    sa->slots = (sa_slot *)calloc(16, sizeof(sa_slot));
    rt_obj_set_finalizer(sa, sa_finalizer);
    return (void *)sa;
}

int64_t rt_sparse_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_sparse_impl *)obj)->count;
}

void *rt_sparse_get(void *obj, int64_t index)
{
    if (!obj)
        return NULL;
    sa_slot *s = sa_find((rt_sparse_impl *)obj, index);
    return s ? s->value : NULL;
}

void rt_sparse_set(void *obj, int64_t index, void *value)
{
    if (!obj)
        return;
    rt_sparse_impl *sa = (rt_sparse_impl *)obj;

    // Check load factor (> 70%)
    if (sa->count * 10 >= sa->capacity * 7)
        sa_grow(sa);

    sa_insert_internal(sa, index, value);
}

int8_t rt_sparse_has(void *obj, int64_t index)
{
    if (!obj)
        return 0;
    return sa_find((rt_sparse_impl *)obj, index) != NULL ? 1 : 0;
}

int8_t rt_sparse_remove(void *obj, int64_t index)
{
    if (!obj)
        return 0;
    rt_sparse_impl *sa = (rt_sparse_impl *)obj;
    sa_slot *s = sa_find(sa, index);
    if (!s)
        return 0;

    rt_obj_release_check0(s->value);
    s->value = NULL;
    s->occupied = 0;
    sa->count--;

    // Rehash subsequent entries that may have been displaced
    int64_t mask = sa->capacity - 1;
    int64_t pos = (int64_t)(s - sa->slots);
    int64_t next = (pos + 1) & mask;

    while (sa->slots[next].occupied)
    {
        sa_slot tmp = sa->slots[next];
        sa->slots[next].occupied = 0;
        sa->count--;
        // Re-insert without ref counting (already held)
        uint64_t h = sa_hash(tmp.key);
        int64_t idx = (int64_t)(h & (uint64_t)mask);
        for (int64_t i = 0; i < sa->capacity; i++)
        {
            int64_t slot = (idx + i) & mask;
            if (!sa->slots[slot].occupied)
            {
                sa->slots[slot] = tmp;
                sa->count++;
                break;
            }
        }
        next = (next + 1) & mask;
    }

    return 1;
}

void *rt_sparse_indices(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_sparse_impl *sa = (rt_sparse_impl *)obj;
    for (int64_t i = 0; i < sa->capacity; i++)
    {
        if (sa->slots[i].occupied)
            rt_seq_push(seq, (void *)sa->slots[i].key);
    }
    return seq;
}

void *rt_sparse_values(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_sparse_impl *sa = (rt_sparse_impl *)obj;
    for (int64_t i = 0; i < sa->capacity; i++)
    {
        if (sa->slots[i].occupied)
            rt_seq_push(seq, sa->slots[i].value);
    }
    return seq;
}

void rt_sparse_clear(void *obj)
{
    if (!obj)
        return;
    rt_sparse_impl *sa = (rt_sparse_impl *)obj;
    for (int64_t i = 0; i < sa->capacity; i++)
    {
        if (sa->slots[i].occupied)
        {
            rt_obj_release_check0(sa->slots[i].value);
            sa->slots[i].occupied = 0;
            sa->slots[i].value = NULL;
        }
    }
    sa->count = 0;
}
