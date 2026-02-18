//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_orderedmap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal structure: doubly-linked list + hash table
// ---------------------------------------------------------------------------

typedef struct rt_om_entry
{
    char *key;
    size_t key_len;
    void *value;
    struct rt_om_entry *hash_next; // Hash chain
    struct rt_om_entry *prev;      // Insertion order
    struct rt_om_entry *next;      // Insertion order
} rt_om_entry;

typedef struct
{
    void *vptr;
    rt_om_entry **buckets;
    int64_t capacity;
    int64_t count;
    rt_om_entry *head; // First inserted
    rt_om_entry *tail; // Last inserted
} rt_orderedmap_impl;

// ---------------------------------------------------------------------------
// Hash helpers
// ---------------------------------------------------------------------------

static uint64_t om_hash(const char *key, size_t len)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++)
    {
        h ^= (uint64_t)(unsigned char)key[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static rt_om_entry *om_find(rt_orderedmap_impl *m, const char *key, size_t len)
{
    uint64_t idx = om_hash(key, len) % (uint64_t)m->capacity;
    rt_om_entry *e = m->buckets[idx];
    while (e)
    {
        if (e->key_len == len && memcmp(e->key, key, len) == 0)
            return e;
        e = e->hash_next;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

static void om_resize(rt_orderedmap_impl *m)
{
    // Guard against integer overflow before doubling.
    if (m->capacity > INT64_MAX / 2)
        rt_trap("OrderedMap: capacity overflow during resize");

    int64_t new_cap = m->capacity * 2;
    rt_om_entry **new_buckets = (rt_om_entry **)calloc((size_t)new_cap, sizeof(rt_om_entry *));
    if (!new_buckets)
        rt_trap("OrderedMap: memory allocation failed during resize");

    // Re-hash all entries via insertion-order list
    rt_om_entry *e = m->head;
    while (e)
    {
        uint64_t idx = om_hash(e->key, e->key_len) % (uint64_t)new_cap;
        e->hash_next = new_buckets[idx];
        new_buckets[idx] = e;
        e = e->next;
    }

    free(m->buckets);
    m->buckets = new_buckets;
    m->capacity = new_cap;
}

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

static void orderedmap_finalizer(void *obj)
{
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)obj;
    rt_om_entry *e = m->head;
    while (e)
    {
        rt_om_entry *next = e->next;
        free(e->key);
        if (e->value)
            rt_obj_release_check0(e->value);
        free(e);
        e = next;
    }
    free(m->buckets);
    m->buckets = NULL;
    m->head = m->tail = NULL;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

void *rt_orderedmap_new(void)
{
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)rt_obj_new_i64(0, sizeof(rt_orderedmap_impl));
    m->capacity = 16;
    m->count = 0;
    m->buckets = (rt_om_entry **)calloc(16, sizeof(rt_om_entry *));
    m->head = m->tail = NULL;
    rt_obj_set_finalizer(m, orderedmap_finalizer);
    return m;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int64_t rt_orderedmap_len(void *map)
{
    if (!map)
        return 0;
    return ((rt_orderedmap_impl *)map)->count;
}

int64_t rt_orderedmap_is_empty(void *map)
{
    if (!map)
        return 1;
    return ((rt_orderedmap_impl *)map)->count == 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Set
// ---------------------------------------------------------------------------

void rt_orderedmap_set(void *map, rt_string key, void *value)
{
    if (!map || !key)
        return;
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)map;

    const char *kstr = rt_string_cstr(key);
    if (!kstr)
        return;
    size_t klen = strlen(kstr);

    // Check for existing key
    rt_om_entry *existing = om_find(m, kstr, klen);
    if (existing)
    {
        // Update value in-place (preserves order)
        if (value)
            rt_obj_retain_maybe(value);
        if (existing->value)
            rt_obj_release_check0(existing->value);
        existing->value = value;
        return;
    }

    // Resize if needed
    if (m->count * 4 >= m->capacity * 3)
        om_resize(m);

    // Create new entry
    rt_om_entry *e = (rt_om_entry *)calloc(1, sizeof(rt_om_entry));
    e->key = (char *)malloc(klen + 1);
    memcpy(e->key, kstr, klen + 1);
    e->key_len = klen;
    if (value)
        rt_obj_retain_maybe(value);
    e->value = value;

    // Add to hash chain
    uint64_t idx = om_hash(kstr, klen) % (uint64_t)m->capacity;
    e->hash_next = m->buckets[idx];
    m->buckets[idx] = e;

    // Add to insertion-order list (tail)
    e->prev = m->tail;
    e->next = NULL;
    if (m->tail)
        m->tail->next = e;
    else
        m->head = e;
    m->tail = e;

    m->count++;
}

// ---------------------------------------------------------------------------
// Get / Has
// ---------------------------------------------------------------------------

void *rt_orderedmap_get(void *map, rt_string key)
{
    if (!map || !key)
        return NULL;
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)map;

    const char *kstr = rt_string_cstr(key);
    if (!kstr)
        return NULL;
    size_t klen = strlen(kstr);

    rt_om_entry *e = om_find(m, kstr, klen);
    return e ? e->value : NULL;
}

int64_t rt_orderedmap_has(void *map, rt_string key)
{
    if (!map || !key)
        return 0;
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)map;

    const char *kstr = rt_string_cstr(key);
    if (!kstr)
        return 0;
    size_t klen = strlen(kstr);

    return om_find(m, kstr, klen) != NULL ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Remove
// ---------------------------------------------------------------------------

int64_t rt_orderedmap_remove(void *map, rt_string key)
{
    if (!map || !key)
        return 0;
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)map;

    const char *kstr = rt_string_cstr(key);
    if (!kstr)
        return 0;
    size_t klen = strlen(kstr);

    uint64_t idx = om_hash(kstr, klen) % (uint64_t)m->capacity;

    // Remove from hash chain
    rt_om_entry **pp = &m->buckets[idx];
    while (*pp)
    {
        rt_om_entry *e = *pp;
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0)
        {
            *pp = e->hash_next;

            // Remove from insertion-order list
            if (e->prev)
                e->prev->next = e->next;
            else
                m->head = e->next;
            if (e->next)
                e->next->prev = e->prev;
            else
                m->tail = e->prev;

            free(e->key);
            if (e->value)
                rt_obj_release_check0(e->value);
            free(e);
            m->count--;
            return 1;
        }
        pp = &e->hash_next;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Keys / Values
// ---------------------------------------------------------------------------

void *rt_orderedmap_keys(void *map)
{
    void *seq = rt_seq_new();
    if (!map)
        return seq;
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)map;

    rt_om_entry *e = m->head;
    while (e)
    {
        rt_string k = rt_string_from_bytes(e->key, e->key_len);
        rt_seq_push(seq, k);
        e = e->next;
    }
    return seq;
}

void *rt_orderedmap_values(void *map)
{
    void *seq = rt_seq_new();
    if (!map)
        return seq;
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)map;

    rt_om_entry *e = m->head;
    while (e)
    {
        if (e->value)
            rt_seq_push(seq, e->value);
        else
            rt_seq_push(seq, NULL);
        e = e->next;
    }
    return seq;
}

rt_string rt_orderedmap_key_at(void *map, int64_t index)
{
    if (!map)
        return NULL;
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)map;

    if (index < 0 || index >= m->count)
        return NULL;

    rt_om_entry *e = m->head;
    for (int64_t i = 0; i < index; i++)
        e = e->next;

    return rt_string_from_bytes(e->key, e->key_len);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void rt_orderedmap_clear(void *map)
{
    if (!map)
        return;
    rt_orderedmap_impl *m = (rt_orderedmap_impl *)map;

    rt_om_entry *e = m->head;
    while (e)
    {
        rt_om_entry *next = e->next;
        free(e->key);
        if (e->value)
            rt_obj_release_check0(e->value);
        free(e);
        e = next;
    }

    memset(m->buckets, 0, (size_t)m->capacity * sizeof(rt_om_entry *));
    m->head = m->tail = NULL;
    m->count = 0;
}
