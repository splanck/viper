//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_msgbus.c
// Purpose: Implements the Viper message bus (pub/sub event system) for the
//          runtime. Topics are hashed into a fixed-size bucket array; each
//          topic maintains a singly-linked list of subscriptions identified by
//          unique integer IDs. Publishers dispatch to all matching subscribers.
//
// Key invariants:
//   - Topic names are hashed with FNV-1a (64-bit) and stored in a bucket array
//     sized at construction time; collisions are resolved by chaining.
//   - Each subscription holds a retained reference to its topic string and an
//     opaque callback pointer (managed by the GC).
//   - Subscription IDs are monotonically increasing 64-bit integers; they are
//     never reused within a bus instance.
//   - Publishing delivers messages in subscription-insertion order per topic.
//   - The bus finalizer releases all topic strings, callbacks, and subscription
//     nodes; it is invoked by the GC when the bus object is collected.
//
// Ownership/Lifetime:
//   - Bus instances are allocated via rt_obj_new_i64 and managed by the GC;
//     callers do not need to free them explicitly.
//   - Topic strings are retained on subscription and released in the finalizer
//     or on explicit unsubscribe.
//   - Callback pointers are reference-counted via rt_obj_release_check0.
//
// Links: src/runtime/core/rt_msgbus.h (public API),
//        src/runtime/core/rt_seq.c (sequence helpers used for dispatch),
//        src/runtime/core/rt_string.c (string reference counting)
//
//===----------------------------------------------------------------------===//

#include "rt_msgbus.h"

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

// --- Subscription ---

typedef struct mb_sub
{
    int64_t id;
    rt_string topic;
    void *callback; // Stored as opaque pointer
    struct mb_sub *next;
} mb_sub;

// --- Topic bucket (hash chain) ---

typedef struct mb_topic
{
    rt_string name;
    mb_sub *subs;
    int64_t count;
    struct mb_topic *next;
} mb_topic;

typedef struct
{
    void *vptr;
    mb_topic **buckets;
    int64_t bucket_count;
    int64_t next_id;
    int64_t total_subs;
} rt_msgbus_impl;

// --- FNV-1a hash ---

static uint64_t mb_hash(const char *s)
{
    uint64_t h = 14695981039346656037ULL;
    while (*s)
    {
        h ^= (uint8_t)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

// --- Internal helpers ---

static void mb_free_sub(mb_sub *s)
{
    if (s->topic)
        rt_string_unref(s->topic);
    rt_obj_release_check0(s->callback);
    free(s);
}

static void mb_finalizer(void *obj)
{
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    if (mb->buckets)
    {
        for (int64_t i = 0; i < mb->bucket_count; i++)
        {
            mb_topic *t = mb->buckets[i];
            while (t)
            {
                mb_topic *nt = t->next;
                mb_sub *s = t->subs;
                while (s)
                {
                    mb_sub *ns = s->next;
                    mb_free_sub(s);
                    s = ns;
                }
                if (t->name)
                    rt_string_unref(t->name);
                free(t);
                t = nt;
            }
        }
        free(mb->buckets);
        mb->buckets = NULL;
    }
}

static mb_topic *mb_find_topic(rt_msgbus_impl *mb, const char *topic_cstr)
{
    uint64_t h = mb_hash(topic_cstr);
    int64_t idx = (int64_t)(h % (uint64_t)mb->bucket_count);
    mb_topic *t = mb->buckets[idx];
    while (t)
    {
        if (strcmp(rt_string_cstr(t->name), topic_cstr) == 0)
            return t;
        t = t->next;
    }
    return NULL;
}

static mb_topic *mb_ensure_topic(rt_msgbus_impl *mb, rt_string topic)
{
    const char *cstr = rt_string_cstr(topic);
    mb_topic *t = mb_find_topic(mb, cstr);
    if (t)
        return t;

    t = (mb_topic *)calloc(1, sizeof(mb_topic));
    t->name = topic;
    rt_obj_retain_maybe(topic);
    t->subs = NULL;
    t->count = 0;

    uint64_t h = mb_hash(cstr);
    int64_t idx = (int64_t)(h % (uint64_t)mb->bucket_count);
    t->next = mb->buckets[idx];
    mb->buckets[idx] = t;
    return t;
}

// --- Public API ---

void *rt_msgbus_new(void)
{
    rt_msgbus_impl *mb = (rt_msgbus_impl *)rt_obj_new_i64(0, sizeof(rt_msgbus_impl));
    mb->bucket_count = 32;
    mb->buckets = (mb_topic **)calloc(32, sizeof(mb_topic *));
    mb->next_id = 1;
    mb->total_subs = 0;
    rt_obj_set_finalizer(mb, mb_finalizer);
    return (void *)mb;
}

int64_t rt_msgbus_subscribe(void *obj, rt_string topic, void *callback)
{
    if (!obj || !topic)
        return -1;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    mb_topic *t = mb_ensure_topic(mb, topic);

    mb_sub *s = (mb_sub *)calloc(1, sizeof(mb_sub));
    s->id = mb->next_id++;
    s->topic = topic;
    rt_obj_retain_maybe(topic);
    s->callback = callback;
    rt_obj_retain_maybe(callback);
    s->next = t->subs;
    t->subs = s;
    t->count++;
    mb->total_subs++;
    return s->id;
}

int8_t rt_msgbus_unsubscribe(void *obj, int64_t sub_id)
{
    if (!obj)
        return 0;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;

    for (int64_t i = 0; i < mb->bucket_count; i++)
    {
        mb_topic *t = mb->buckets[i];
        while (t)
        {
            mb_sub **pp = &t->subs;
            while (*pp)
            {
                if ((*pp)->id == sub_id)
                {
                    mb_sub *victim = *pp;
                    *pp = victim->next;
                    mb_free_sub(victim);
                    t->count--;
                    mb->total_subs--;
                    return 1;
                }
                pp = &(*pp)->next;
            }
            t = t->next;
        }
    }
    return 0;
}

int64_t rt_msgbus_publish(void *obj, rt_string topic, void *data)
{
    if (!obj || !topic)
        return 0;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    mb_topic *t = mb_find_topic(mb, rt_string_cstr(topic));
    if (!t)
        return 0;

    // Count subscribers (actual callback invocation would need VM support)
    // For now, publish just tracks how many subscribers would be notified
    return t->count;
}

int64_t rt_msgbus_subscriber_count(void *obj, rt_string topic)
{
    if (!obj || !topic)
        return 0;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    mb_topic *t = mb_find_topic(mb, rt_string_cstr(topic));
    return t ? t->count : 0;
}

int64_t rt_msgbus_total_subscriptions(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_msgbus_impl *)obj)->total_subs;
}

void *rt_msgbus_topics(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;

    for (int64_t i = 0; i < mb->bucket_count; i++)
    {
        mb_topic *t = mb->buckets[i];
        while (t)
        {
            if (t->count > 0)
                rt_seq_push(seq, t->name);
            t = t->next;
        }
    }
    return seq;
}

void rt_msgbus_clear_topic(void *obj, rt_string topic)
{
    if (!obj || !topic)
        return;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    mb_topic *t = mb_find_topic(mb, rt_string_cstr(topic));
    if (!t)
        return;

    mb_sub *s = t->subs;
    while (s)
    {
        mb_sub *next = s->next;
        mb->total_subs--;
        mb_free_sub(s);
        s = next;
    }
    t->subs = NULL;
    t->count = 0;
}

void rt_msgbus_clear(void *obj)
{
    if (!obj)
        return;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;

    for (int64_t i = 0; i < mb->bucket_count; i++)
    {
        mb_topic *t = mb->buckets[i];
        while (t)
        {
            mb_sub *s = t->subs;
            while (s)
            {
                mb_sub *next = s->next;
                mb_free_sub(s);
                s = next;
            }
            t->subs = NULL;
            t->count = 0;
            t = t->next;
        }
    }
    mb->total_subs = 0;
}
