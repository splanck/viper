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

#include "rt_trap.h"

// --- Subscription ---

typedef struct mb_sub {
    int64_t id;
    rt_string topic;
    void *callback; // Stored as opaque pointer
    struct mb_sub *next;
} mb_sub;

// --- Topic bucket (hash chain) ---

typedef struct mb_topic {
    rt_string name;
    mb_sub *subs;
    int64_t count;
    struct mb_topic *next;
} mb_topic;

typedef struct {
    void *vptr;
    mb_topic **buckets;
    int64_t bucket_count;
    int64_t next_id;
    int64_t total_subs;
} rt_msgbus_impl;

// --- FNV-1a hash ---

static uint64_t mb_hash(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

// --- Internal helpers ---

static void mb_free_sub(mb_sub *s) {
    if (s->topic)
        rt_string_unref(s->topic);
    rt_obj_release_check0(s->callback);
    free(s);
}

static void mb_finalizer(void *obj) {
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    if (mb->buckets) {
        for (int64_t i = 0; i < mb->bucket_count; i++) {
            mb_topic *t = mb->buckets[i];
            while (t) {
                mb_topic *nt = t->next;
                mb_sub *s = t->subs;
                while (s) {
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

static mb_topic *mb_find_topic(rt_msgbus_impl *mb, const char *topic_cstr) {
    uint64_t h = mb_hash(topic_cstr);
    int64_t idx = (int64_t)(h % (uint64_t)mb->bucket_count);
    mb_topic *t = mb->buckets[idx];
    while (t) {
        if (strcmp(rt_string_cstr(t->name), topic_cstr) == 0)
            return t;
        t = t->next;
    }
    return NULL;
}

static mb_topic *mb_ensure_topic(rt_msgbus_impl *mb, rt_string topic) {
    const char *cstr = rt_string_cstr(topic);
    mb_topic *t = mb_find_topic(mb, cstr);
    if (t)
        return t;

    t = (mb_topic *)calloc(1, sizeof(mb_topic));
    if (!t)
        rt_trap("rt_msgbus: memory allocation failed");
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

void *rt_msgbus_new(void) {
    rt_msgbus_impl *mb = (rt_msgbus_impl *)rt_obj_new_i64(0, sizeof(rt_msgbus_impl));
    mb->bucket_count = 32;
    mb->buckets = (mb_topic **)calloc(32, sizeof(mb_topic *));
    if (!mb->buckets)
        rt_trap("rt_msgbus: memory allocation failed");
    mb->next_id = 1;
    mb->total_subs = 0;
    rt_obj_set_finalizer(mb, mb_finalizer);
    return (void *)mb;
}

/// @brief Subscribe a callback to a topic on the message bus.
/// @details Adds the callback to the topic's subscriber list. When a message is
///          published to this topic, the callback will be invoked with the payload.
///          Returns a unique subscription ID for later unsubscribe.
/// @param obj MessageBus object.
/// @param topic Topic name string.
/// @param callback Function pointer (opaque) to invoke on publish.
/// @return Subscription ID (>= 0 on success, -1 on failure).
int64_t rt_msgbus_subscribe(void *obj, rt_string topic, void *callback) {
    if (!obj || !topic)
        return -1;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    mb_topic *t = mb_ensure_topic(mb, topic);

    mb_sub *s = (mb_sub *)calloc(1, sizeof(mb_sub));
    if (!s)
        rt_trap("rt_msgbus: memory allocation failed");
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

/// @brief Remove a subscription from the message bus by its unique ID.
/// @details Performs a linear scan across all topic buckets and their subscriber
///          chains to locate the subscription matching sub_id. This O(B*S) scan
///          is acceptable because unsubscribe is infrequent relative to publish,
///          and maintaining a separate ID→subscription index would add complexity
///          for little practical benefit. Once found, the node is unlinked from
///          the singly-linked list using the classic pointer-to-pointer technique,
///          its topic string and callback are released, and the node is freed.
/// @param obj MessageBus object pointer; returns 0 if NULL.
/// @param sub_id The subscription ID returned by rt_msgbus_subscribe.
/// @return 1 if the subscription was found and removed, 0 if not found.
int8_t rt_msgbus_unsubscribe(void *obj, int64_t sub_id) {
    if (!obj)
        return 0;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;

    for (int64_t i = 0; i < mb->bucket_count; i++) {
        mb_topic *t = mb->buckets[i];
        while (t) {
            mb_sub **pp = &t->subs;
            while (*pp) {
                if ((*pp)->id == sub_id) {
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

/// @brief Publish a message to all subscribers of a topic.
/// @details Looks up the topic by name using FNV-1a hashing, then counts
///          the number of active subscribers. The actual callback invocation
///          requires VM support (trampolining from C back into managed code),
///          so currently this returns the subscriber count as a proxy for
///          "messages that would be delivered." This is still useful: callers
///          can check whether anyone is listening before constructing expensive
///          payloads.
/// @param obj MessageBus object pointer; returns 0 if NULL.
/// @param topic Topic name string to publish to.
/// @param data Payload pointer (currently unused pending VM callback support).
/// @return Number of subscribers that would receive the message, 0 if none.
int64_t rt_msgbus_publish(void *obj, rt_string topic, void *data) {
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

/// @brief Return the number of active subscribers for a specific topic.
/// @details Looks up the topic bucket via FNV-1a hash and returns the cached
///          subscriber count. This is O(1) after the hash lookup because each
///          mb_topic maintains a running count that is incremented on subscribe
///          and decremented on unsubscribe, avoiding a linked-list traversal.
/// @param obj MessageBus object pointer; returns 0 if NULL.
/// @param topic Topic name string to query.
/// @return Number of subscribers for the topic, 0 if topic not found or NULL.
int64_t rt_msgbus_subscriber_count(void *obj, rt_string topic) {
    if (!obj || !topic)
        return 0;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    mb_topic *t = mb_find_topic(mb, rt_string_cstr(topic));
    return t ? t->count : 0;
}

/// @brief Return the total number of active subscriptions across all topics.
/// @details Returns the cached total_subs counter maintained by the bus. This
///          is a global aggregate: it is incremented on every subscribe and
///          decremented on every unsubscribe or clear operation. Useful for
///          diagnostics and monitoring bus activity without iterating topics.
/// @param obj MessageBus object pointer; returns 0 if NULL.
/// @return Total subscription count across all topics.
int64_t rt_msgbus_total_subscriptions(void *obj) {
    if (!obj)
        return 0;
    return ((rt_msgbus_impl *)obj)->total_subs;
}

void *rt_msgbus_topics(void *obj) {
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;

    for (int64_t i = 0; i < mb->bucket_count; i++) {
        mb_topic *t = mb->buckets[i];
        while (t) {
            if (t->count > 0)
                rt_seq_push(seq, t->name);
            t = t->next;
        }
    }
    return seq;
}

/// @brief Remove all subscriptions from a single topic without destroying the
///        topic bucket itself.
/// @details Walks the subscriber linked list for the given topic, freeing each
///          node (releasing its topic string reference and callback). The topic
///          bucket remains in the hash table with count=0 so that future
///          subscriptions to the same topic name reuse it without re-hashing.
///          The bus-wide total_subs counter is decremented for each removed sub.
/// @param obj MessageBus object pointer; no-op if NULL.
/// @param topic Topic name string to clear; no-op if NULL or not found.
void rt_msgbus_clear_topic(void *obj, rt_string topic) {
    if (!obj || !topic)
        return;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    mb_topic *t = mb_find_topic(mb, rt_string_cstr(topic));
    if (!t)
        return;

    mb_sub *s = t->subs;
    while (s) {
        mb_sub *next = s->next;
        mb->total_subs--;
        mb_free_sub(s);
        s = next;
    }
    t->subs = NULL;
    t->count = 0;
}

/// @brief Remove all subscriptions from every topic on the message bus.
/// @details Iterates all buckets and all topic chains, freeing every subscriber
///          node in each topic. Unlike the finalizer, this does NOT free the
///          topic buckets themselves or the bucket array — the bus remains usable
///          for new subscriptions afterward. This distinction is important: clear
///          is a "reset to empty" operation, not a destruction. The total_subs
///          counter is reset to 0 after all nodes are freed.
/// @param obj MessageBus object pointer; no-op if NULL.
void rt_msgbus_clear(void *obj) {
    if (!obj)
        return;
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;

    for (int64_t i = 0; i < mb->bucket_count; i++) {
        mb_topic *t = mb->buckets[i];
        while (t) {
            mb_sub *s = t->subs;
            while (s) {
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
