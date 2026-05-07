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
//   - Callback pointers are retained on subscribe and released on unsubscribe,
//     clear, or finalization.
//
// Links: src/runtime/core/rt_msgbus.h (public API),
//        src/runtime/core/rt_seq.c (sequence helpers used for dispatch),
//        src/runtime/core/rt_string.c (string reference counting)
//
//===----------------------------------------------------------------------===//

#include "rt_msgbus.h"

#include "rt_gc.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <limits.h>
#include <stdio.h>
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

typedef struct {
    void *vptr;
    rt_msgbus_callback_fn fn;
} rt_msgbus_callback_impl;

// --- FNV-1a hash ---

/// @brief Compute the FNV-1a 64-bit hash of a NUL-terminated string.
/// @details Standard FNV-1a constants: 64-bit offset basis
///          `0xcbf29ce484222325` and prime `0x100000001b3`. Good
///          distribution for short topic strings ("user.login",
///          "tick", etc.) without the cost of a CSPRNG-seeded
///          hash like SipHash. Used only to bucket topic names —
///          collisions are handled by per-bucket linear probe.
static uint64_t mb_hash_bytes(const char *s, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint8_t)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// --- Internal helpers ---

static int mb_topic_view(rt_string topic, const char **bytes, size_t *len) {
    if (bytes)
        *bytes = NULL;
    if (len)
        *len = 0;
    if (!topic)
        return 0;
    if (!rt_string_is_handle(topic)) {
        rt_trap("rt_msgbus: invalid topic string");
        return 0;
    }
    int64_t n = rt_str_len(topic);
    if (n < 0) {
        rt_trap("rt_msgbus: invalid topic length");
        return 0;
    }
    const char *data = rt_string_cstr(topic);
    if (!data)
        return 0;
    if (bytes)
        *bytes = data;
    if (len)
        *len = (size_t)n;
    return 1;
}

static rt_msgbus_impl *mb_require(void *obj, const char *fn_name) {
    if (!obj)
        return NULL;
    if (rt_obj_class_id(obj) != RT_MSGBUS_CLASS_ID) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s: invalid MessageBus object", fn_name);
        rt_trap(buf);
        return NULL;
    }
    return (rt_msgbus_impl *)obj;
}

static int mb_callback_is_native(void *callback) {
    if (!callback)
        return 0;
    if (rt_string_is_handle(callback))
        return 0;
    rt_heap_hdr_t *hdr = NULL;
    if (rt_heap_try_get_header(callback, &hdr))
        return hdr != NULL;
    return 1;
}

static int mb_invoke_callback(void *callback, void *data) {
    if (!callback)
        return 0;
    rt_heap_hdr_t *hdr = NULL;
    if (rt_heap_try_get_header(callback, &hdr)) {
        if (!hdr || hdr->class_id != RT_MSGBUS_CALLBACK_CLASS_ID) {
            rt_trap("rt_msgbus_publish: subscriber callback is not callable");
            return 0;
        }
        rt_msgbus_callback_impl *cb = (rt_msgbus_callback_impl *)callback;
        if (!cb->fn)
            return 0;
        cb->fn(data);
        return 1;
    }
    if (rt_string_is_handle(callback)) {
        rt_trap("rt_msgbus_publish: string subscriber callback is not callable");
        return 0;
    }
    ((rt_msgbus_callback_fn)callback)(data);
    return 1;
}

/// @brief Free a single subscriber node (drops topic ref + callback ref).
/// @details Called by `mb_finalizer` (full-bus teardown) and by the
///          unsubscribe path. The callback is treated as a GC-managed
///          opaque object so functions and bound methods both work.
static void mb_free_sub(mb_sub *s) {
    if (s->topic)
        rt_string_unref(s->topic);
    if (rt_obj_release_check0(s->callback))
        rt_obj_free(s->callback);
    free(s);
}

/// @brief GC finalizer — walk every bucket and free every topic + subscriber.
/// @details Two-level walk: each bucket holds a linked list of
///          `mb_topic` nodes; each topic holds its own linked list
///          of subscribers. Releases topic-name refs and frees
///          everything before nulling the bucket array.
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

static void mb_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    if (!mb || !visitor || !mb->buckets)
        return;
    for (int64_t i = 0; i < mb->bucket_count; i++) {
        mb_topic *t = mb->buckets[i];
        while (t) {
            for (mb_sub *s = t->subs; s; s = s->next) {
                if (s->callback && rt_heap_is_payload(s->callback))
                    visitor(s->callback, ctx);
            }
            t = t->next;
        }
    }
}

/// @brief Look up the topic node for `topic_cstr` (returns NULL if not present).
/// @details Hash → bucket → linear probe down the per-bucket chain.
///          Bucket count is fixed at construction (32 by default) —
///          rehashing isn't implemented because typical message-bus
///          workloads have <100 topics, well under the load that
///          would warrant resizing.
static mb_topic *mb_find_topic(rt_msgbus_impl *mb, rt_string topic) {
    const char *topic_bytes = NULL;
    size_t topic_len = 0;
    if (!mb_topic_view(topic, &topic_bytes, &topic_len))
        return NULL;

    uint64_t h = mb_hash_bytes(topic_bytes, topic_len);
    int64_t idx = (int64_t)(h % (uint64_t)mb->bucket_count);
    mb_topic *t = mb->buckets[idx];
    while (t) {
        size_t name_len = (size_t)rt_str_len(t->name);
        const char *name_bytes = rt_string_cstr(t->name);
        if (name_len == topic_len && memcmp(name_bytes, topic_bytes, topic_len) == 0)
            return t;
        t = t->next;
    }
    return NULL;
}

/// @brief Look up or create the topic node for `topic`.
/// @details Find-or-insert: returns the existing node when present,
///          otherwise allocates a fresh one, retains the topic-name
///          string, and inserts at the head of its bucket's chain.
///          Subscribers list starts empty — the caller appends.
static mb_topic *mb_ensure_topic(rt_msgbus_impl *mb, rt_string topic) {
    const char *topic_bytes = NULL;
    size_t topic_len = 0;
    if (!mb_topic_view(topic, &topic_bytes, &topic_len))
        return NULL;
    mb_topic *t = mb_find_topic(mb, topic);
    if (t)
        return t;

    t = (mb_topic *)calloc(1, sizeof(mb_topic));
    if (!t)
        rt_trap("rt_msgbus: memory allocation failed");
    t->name = topic;
    rt_obj_retain_maybe(topic);
    t->subs = NULL;
    t->count = 0;

    uint64_t h = mb_hash_bytes(topic_bytes, topic_len);
    int64_t idx = (int64_t)(h % (uint64_t)mb->bucket_count);
    t->next = mb->buckets[idx];
    mb->buckets[idx] = t;
    return t;
}

// --- Public API ---

/// @brief Create a new MessageBus with 32 hash buckets and ID counter starting at 1.
/// @details Subscriber IDs start at 1 so 0 can act as a "no subscription"
///          sentinel without ambiguity. Bucket count is small but
///          typical message-bus workloads have few enough topics that
///          collision chains stay short.
void *rt_msgbus_new(void) {
    mb_topic **buckets = (mb_topic **)calloc(32, sizeof(mb_topic *));
    if (!buckets) {
        rt_trap("rt_msgbus: memory allocation failed");
        return NULL;
    }

    rt_msgbus_impl *mb =
        (rt_msgbus_impl *)rt_obj_new_i64(RT_MSGBUS_CLASS_ID, (int64_t)sizeof(rt_msgbus_impl));
    if (!mb) {
        free(buckets);
        return NULL;
    }
    mb->bucket_count = 32;
    mb->buckets = buckets;
    mb->next_id = 1;
    mb->total_subs = 0;
    rt_obj_set_finalizer(mb, mb_finalizer);
    rt_gc_track(mb, mb_traverse);
    return (void *)mb;
}

void *rt_msgbus_callback_new(void *callback) {
    if (!callback)
        return NULL;
    rt_msgbus_callback_impl *cb = (rt_msgbus_callback_impl *)rt_obj_new_i64(
        RT_MSGBUS_CALLBACK_CLASS_ID, (int64_t)sizeof(rt_msgbus_callback_impl));
    if (!cb)
        return NULL;
    cb->fn = (rt_msgbus_callback_fn)callback;
    return cb;
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
    if (!obj || !topic || !callback)
        return -1;
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_subscribe");
    if (!mb)
        return -1;
    if (!mb_callback_is_native(callback)) {
        rt_trap("rt_msgbus_subscribe: callback must be a native function pointer or MessageBus callback object");
        return -1;
    }
    if (mb->next_id <= 0 || mb->next_id == INT64_MAX) {
        rt_trap("rt_msgbus_subscribe: subscription id overflow");
        return -1;
    }
    mb_topic *t = mb_ensure_topic(mb, topic);
    if (!t)
        return -1;

    mb_sub *s = (mb_sub *)calloc(1, sizeof(mb_sub));
    if (!s)
        rt_trap("rt_msgbus: memory allocation failed");
    s->id = mb->next_id++;
    s->topic = topic;
    rt_obj_retain_maybe(topic);
    s->callback = callback;
    rt_obj_retain_maybe(callback);
    s->next = NULL;
    if (!t->subs) {
        t->subs = s;
    } else {
        mb_sub *tail = t->subs;
        while (tail->next)
            tail = tail->next;
        tail->next = s;
    }
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
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_unsubscribe");
    if (!mb)
        return 0;

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
/// @details Looks up the topic by name using FNV-1a hashing, snapshots the
///          current subscriber list, then invokes each callback synchronously
///          in subscription-insertion order. Unsubscribe/Clear during a publish
///          affect future publishes but do not invalidate the in-flight snapshot.
/// @param obj MessageBus object pointer; returns 0 if NULL.
/// @param topic Topic name string to publish to.
/// @param data Payload pointer passed to each callback.
/// @return Number of subscribers notified, 0 if none.
int64_t rt_msgbus_publish(void *obj, rt_string topic, void *data) {
    if (!obj || !topic)
        return 0;
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_publish");
    if (!mb)
        return 0;
    mb_topic *t = mb_find_topic(mb, topic);
    if (!t || t->count <= 0)
        return 0;

    void **callbacks = (void **)calloc((size_t)t->count, sizeof(void *));
    if (!callbacks)
        rt_trap("rt_msgbus: memory allocation failed");

    int64_t count = 0;
    for (mb_sub *s = t->subs; s; s = s->next) {
        callbacks[count++] = s->callback;
        rt_obj_retain_maybe(s->callback);
    }

    for (int64_t i = 0; i < count; ++i) {
        mb_invoke_callback(callbacks[i], data);
        if (rt_obj_release_check0(callbacks[i]))
            rt_obj_free(callbacks[i]);
    }

    free(callbacks);
    return count;
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
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_subscriber_count");
    if (!mb)
        return 0;
    mb_topic *t = mb_find_topic(mb, topic);
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
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_total_subscriptions");
    return mb ? mb->total_subs : 0;
}

/// @brief Return a Seq containing the names of all topics that have at least one subscriber.
/// @param obj MessageBus object pointer; returns an empty Seq if NULL.
/// @return Seq of rt_string topic names (caller-owned via GC).
void *rt_msgbus_topics(void *obj) {
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_topics");
    if (!mb)
        return seq;

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
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_clear_topic");
    if (!mb)
        return;
    mb_topic *t = mb_find_topic(mb, topic);
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
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_clear");
    if (!mb)
        return;

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
