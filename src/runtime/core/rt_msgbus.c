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
//   - Managed publish payloads are retained while the subscriber snapshot is
//     invoked; raw foreign pointers are borrowed for the duration of the call.
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
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <setjmp.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RT_PLATFORM_WINDOWS
#include <windows.h>
#elif !RT_PLATFORM_VIPERDOS
#include <sched.h>
#endif

#include "rt_trap.h"

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

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
    int lock;
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

static void mb_lock(rt_msgbus_impl *mb) {
    if (!mb)
        return;
    if (__atomic_test_and_set(&mb->lock, __ATOMIC_ACQUIRE)) {
        do {
#if RT_PLATFORM_WINDOWS
            SwitchToThread();
#elif !RT_PLATFORM_VIPERDOS
            sched_yield();
#endif
        } while (__atomic_test_and_set(&mb->lock, __ATOMIC_ACQUIRE));
    }
}

/// @brief Release the bus's internal spinlock with release ordering.
/// @details No-op on a NULL bus so call-sites that handle a NULL bus uniformly don't have
///          to special-case the unlock.
static void mb_unlock(rt_msgbus_impl *mb) {
    if (mb)
        __atomic_clear(&mb->lock, __ATOMIC_RELEASE);
}

/// @brief Resolve a runtime string into a `(bytes, length)` byte view, validating the handle.
/// @details Used by every public bus operation that takes a topic string. NULL traps are
///          deliberately silent (the caller treats it as "no topic"), but a non-NULL value
///          that fails `rt_string_is_handle` raises a runtime trap — callers should never
///          be passing arbitrary memory as a topic. Returns 1 when @p bytes / @p len carry
///          a usable view, 0 otherwise. The bytes pointer points into the string's storage
///          and is valid only while the caller holds whatever retain it had at call time.
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

/// @brief Validate that @p obj is a live MessageBus instance, trapping on misuse.
/// @details Guards every public entry point that dereferences `rt_msgbus_impl`. NULL is
///          treated as silently absent (returns NULL) so callers can no-op gracefully.
///          Any non-NULL pointer with the wrong runtime class id traps with a message
///          including @p fn_name so the diagnostic identifies the offending API.
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

/// @brief Test whether @p callback is a live `MessageBus.Callback` heap object.
/// @details Callbacks must be wrapped through `rt_msgbus_callback_new` so the bus has a
///          stable C-function pointer it can invoke without risking a freed pointer.
///          Strings, raw function pointers, and other heap classes all return 0 — the
///          caller treats those as misuse and traps. Returns 1 only for objects whose
///          heap-kind is `RT_HEAP_OBJECT` and whose class id matches.
static int mb_callback_is_native(void *callback) {
    if (!callback)
        return 0;
    if (rt_string_is_handle(callback))
        return 0;
    rt_heap_hdr_t *hdr = NULL;
    if (rt_heap_try_get_header(callback, &hdr))
        return hdr && (rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT &&
               hdr->class_id == RT_MSGBUS_CALLBACK_CLASS_ID;
    return 0;
}

/// @brief Invoke a registered callback with @p data, trapping if the handle is no longer valid.
/// @details Re-validates the callback's class id at delivery time — a callback could in
///          principle be freed between subscribe and publish, so the bus checks again
///          rather than trust the original validation. Returns 1 when the callback ran,
///          0 when it had a NULL function pointer (defensive — should not normally happen).
static int mb_invoke_callback(void *callback, void *data) {
    if (!callback)
        return 0;
    rt_heap_hdr_t *hdr = NULL;
    if (rt_heap_try_get_header(callback, &hdr)) {
        if (!hdr || (rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT ||
            hdr->class_id != RT_MSGBUS_CALLBACK_CLASS_ID) {
            rt_trap("rt_msgbus_publish: subscriber callback is not callable");
            return 0;
        }
        rt_msgbus_callback_impl *cb = (rt_msgbus_callback_impl *)callback;
        if (!cb->fn)
            return 0;
        cb->fn(data);
        return 1;
    }
    rt_trap("rt_msgbus_publish: subscriber callback is not callable");
    return 0;
}

/// @brief Retain a publish payload if it's a runtime-managed handle. Returns 1 iff retained.
/// @details Publish payloads can be either managed runtime objects or arbitrary foreign
///          pointers; this helper distinguishes them by checking `rt_string_is_handle` and
///          `rt_heap_is_payload`. Managed payloads are retained for the duration of the
///          subscriber-snapshot delivery so a concurrent free can't pull them away mid-call;
///          foreign pointers are returned as 0 and the caller leaves them as borrowed.
static int mb_retain_managed_payload(void *data) {
    if (!data)
        return 0;
    if (rt_string_is_handle(data) || rt_heap_is_payload(data)) {
        rt_memory_retain(data);
        return 1;
    }
    return 0;
}

/// @brief Free a single subscriber node (drops topic ref + callback ref).
/// @details Called by `mb_finalizer` (full-bus teardown) and by the
///          unsubscribe path. The callback is treated as a GC-managed
///          opaque object so functions and bound methods both work.
static void mb_free_sub(mb_sub *s) {
    if (!s)
        return;
    if (s->topic)
        rt_string_unref(s->topic);
    if (rt_obj_release_check0(s->callback))
        rt_obj_free(s->callback);
    free(s);
}

/// @brief Free a single topic node — releases the topic name and the allocation itself.
/// @details Used as the per-node primitive by `mb_free_topic_chain` and the unsubscribe
///          path. Does *not* walk the topic's subscriber list — callers that need full
///          teardown should use the chain helper, which handles both layers.
static void mb_free_topic_node(mb_topic *t) {
    if (!t)
        return;
    if (t->name)
        rt_string_unref(t->name);
    free(t);
}

/// @brief Free an entire bucket chain of topics, including each topic's subscriber list.
/// @details Two-level walk used by `mb_finalizer`: for each topic node, drain the
///          subscriber list via `mb_free_sub`, then `mb_free_topic_node` to release the
///          name and free the node. Iterative (no recursion) so deep buckets don't
///          consume stack.
static void mb_free_topic_chain(mb_topic *t) {
    while (t) {
        mb_topic *next = t->next;
        mb_sub *s = t->subs;
        while (s) {
            mb_sub *ns = s->next;
            mb_free_sub(s);
            s = ns;
        }
        mb_free_topic_node(t);
        t = next;
    }
}

/// @brief GC finalizer — walk every bucket and free every topic + subscriber.
/// @details Two-level walk: each bucket holds a linked list of
///          `mb_topic` nodes; each topic holds its own linked list
///          of subscribers. Releases topic-name refs and frees
///          everything before nulling the bucket array.
static void mb_finalizer(void *obj) {
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    mb_lock(mb);
    mb_topic **buckets = mb->buckets;
    int64_t bucket_count = mb->bucket_count;
    mb->buckets = NULL;
    mb->bucket_count = 0;
    mb->total_subs = 0;
    mb_unlock(mb);

    if (buckets) {
        for (int64_t i = 0; i < bucket_count; i++) {
            mb_free_topic_chain(buckets[i]);
        }
        free(buckets);
    }
}

/// @brief GC traversal callback for MessageBus instances.
/// @details Walks every bucket and visits each subscriber's retained callback so the GC
///          marker can colour reachable callbacks live. Topic strings are reference-
///          counted independently and don't participate in cycle detection, so they
///          aren't visited here.
static void mb_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    rt_msgbus_impl *mb = (rt_msgbus_impl *)obj;
    if (!mb || !visitor)
        return;

    int64_t count = 0;
    mb_lock(mb);
    if (!mb->buckets) {
        mb_unlock(mb);
        return;
    }
    for (int64_t i = 0; i < mb->bucket_count; i++) {
        mb_topic *t = mb->buckets[i];
        while (t) {
            for (mb_sub *s = t->subs; s; s = s->next) {
                if (s->callback && rt_heap_is_payload(s->callback))
                    count++;
            }
            t = t->next;
        }
    }
    mb_unlock(mb);
    if (count <= 0)
        return;

    void **callbacks = (void **)calloc((size_t)count, sizeof(void *));
    if (!callbacks)
        return;

    int64_t copied = 0;
    mb_lock(mb);
    for (int64_t i = 0; i < mb->bucket_count && copied < count; i++) {
        mb_topic *t = mb->buckets[i];
        while (t && copied < count) {
            for (mb_sub *s = t->subs; s && copied < count; s = s->next) {
                if (s->callback && rt_heap_is_payload(s->callback)) {
                    callbacks[copied++] = s->callback;
                    rt_obj_retain_maybe(s->callback);
                }
            }
            t = t->next;
        }
    }
    mb_unlock(mb);

    for (int64_t i = 0; i < copied; ++i)
        visitor(callbacks[i], ctx);
    for (int64_t i = 0; i < copied; ++i) {
        if (rt_obj_release_check0(callbacks[i]))
            rt_obj_free(callbacks[i]);
    }
    free(callbacks);
}

/// @brief Look up the topic node for `topic_cstr` (returns NULL if not present).
/// @details Hash → bucket → linear probe down the per-bucket chain.
///          Bucket count is fixed at construction (32 by default) —
///          rehashing isn't implemented because typical message-bus
///          workloads have <100 topics, well under the load that
///          would warrant resizing.
static mb_topic *mb_find_topic_view_locked(rt_msgbus_impl *mb,
                                           const char *topic_bytes,
                                           size_t topic_len) {
    if (!mb || !mb->buckets || mb->bucket_count <= 0 || !topic_bytes)
        return NULL;

    uint64_t h = mb_hash_bytes(topic_bytes, topic_len);
    int64_t idx = (int64_t)(h % (uint64_t)mb->bucket_count);
    mb_topic *t = mb->buckets[idx];
    while (t) {
        if (!rt_string_is_handle(t->name)) {
            t = t->next;
            continue;
        }
        size_t name_len = (size_t)rt_str_len(t->name);
        const char *name_bytes = rt_string_cstr(t->name);
        if (name_len == topic_len && name_bytes &&
            memcmp(name_bytes, topic_bytes, topic_len) == 0)
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
static mb_topic *mb_prepare_topic(void) {
    mb_topic *t = (mb_topic *)calloc(1, sizeof(mb_topic));
    if (!t) {
        rt_trap("rt_msgbus: memory allocation failed");
        return NULL;
    }
    return t;
}

/// @brief Look up @p topic; if absent, install the pre-allocated `*spare` and return it.
/// @details Caller-supplied spare pattern: the caller pre-allocates a fresh `mb_topic`
///          (via `mb_prepare_topic`) before entering the lock, so the locked region never
///          itself calls `malloc`. If the topic already exists, returns it and the spare
///          is left untouched (caller frees it). On install, the function consumes the
///          spare (sets `*spare = NULL`), retains the topic name string, and links the
///          new node at the head of its bucket. Returns NULL when the bus is invalid or
///          when the spare pointer is missing.
static mb_topic *mb_ensure_topic_locked(rt_msgbus_impl *mb,
                                        rt_string topic,
                                        const char *topic_bytes,
                                        size_t topic_len,
                                        mb_topic **spare) {
    if (!mb || !mb->buckets || mb->bucket_count <= 0)
        return NULL;
    if (!topic_bytes)
        return NULL;
    mb_topic *t = mb_find_topic_view_locked(mb, topic_bytes, topic_len);
    if (t)
        return t;

    if (!spare || !*spare) {
        rt_trap("rt_msgbus: memory allocation failed");
        return NULL;
    }
    t = *spare;
    *spare = NULL;
    t->name = topic;
    rt_string_ref(topic);
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
    const char *topic_bytes = NULL;
    size_t topic_len = 0;
    if (!mb_topic_view(topic, &topic_bytes, &topic_len))
        return -1;
    if (!mb_callback_is_native(callback)) {
        rt_trap("rt_msgbus_subscribe: callback must be a MessageBus callback object");
        return -1;
    }

    rt_string retained_topic = rt_string_ref(topic);
    void *retained_callback = callback;
    rt_obj_retain_maybe(retained_callback);
    if (!mb_callback_is_native(retained_callback)) {
        rt_string_unref(retained_topic);
        if (rt_obj_release_check0(retained_callback))
            rt_obj_free(retained_callback);
        rt_trap("rt_msgbus_subscribe: callback must be a live MessageBus callback object");
        return -1;
    }

    mb_sub *s = (mb_sub *)calloc(1, sizeof(mb_sub));
    if (!s) {
        rt_string_unref(retained_topic);
        if (rt_obj_release_check0(retained_callback))
            rt_obj_free(retained_callback);
        rt_trap("rt_msgbus: memory allocation failed");
        return -1;
    }
    mb_topic *spare_topic = mb_prepare_topic();
    if (!spare_topic) {
        rt_string_unref(retained_topic);
        if (rt_obj_release_check0(retained_callback))
            rt_obj_free(retained_callback);
        free(s);
        return -1;
    }

    mb_lock(mb);
    if (mb->next_id <= 0) {
        mb_unlock(mb);
        free(s);
        free(spare_topic);
        rt_string_unref(retained_topic);
        if (rt_obj_release_check0(retained_callback))
            rt_obj_free(retained_callback);
        rt_trap("rt_msgbus_subscribe: subscription id overflow");
        return -1;
    }
    mb_topic *t = mb_ensure_topic_locked(mb, topic, topic_bytes, topic_len, &spare_topic);
    if (!t) {
        mb_unlock(mb);
        free(s);
        free(spare_topic);
        rt_string_unref(retained_topic);
        if (rt_obj_release_check0(retained_callback))
            rt_obj_free(retained_callback);
        return -1;
    }

    s->id = mb->next_id;
    mb->next_id = mb->next_id == INT64_MAX ? 0 : mb->next_id + 1;
    s->topic = retained_topic;
    s->callback = retained_callback;
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
    int64_t id = s->id;
    mb_unlock(mb);
    free(spare_topic);
    return id;
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

    mb_sub *victim = NULL;
    mb_topic *empty_topic = NULL;
    mb_lock(mb);
    for (int64_t i = 0; i < mb->bucket_count; i++) {
        mb_topic **tp = &mb->buckets[i];
        while (*tp) {
            mb_topic *t = *tp;
            mb_sub **pp = &t->subs;
            while (*pp) {
                if ((*pp)->id == sub_id) {
                    victim = *pp;
                    *pp = victim->next;
                    victim->next = NULL;
                    t->count--;
                    mb->total_subs--;
                    if (t->count == 0) {
                        empty_topic = t;
                        *tp = t->next;
                        t->next = NULL;
                    }
                    mb_unlock(mb);
                    mb_free_sub(victim);
                    mb_free_topic_node(empty_topic);
                    return 1;
                }
                pp = &(*pp)->next;
            }
            tp = &t->next;
        }
    }
    mb_unlock(mb);
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

    const char *topic_bytes = NULL;
    size_t topic_len = 0;
    if (!mb_topic_view(topic, &topic_bytes, &topic_len))
        return 0;

    mb_lock(mb);
    mb_topic *t = mb_find_topic_view_locked(mb, topic_bytes, topic_len);
    if (!t || t->count <= 0) {
        mb_unlock(mb);
        return 0;
    }

    void **callbacks = (void **)calloc((size_t)t->count, sizeof(void *));
    if (!callbacks) {
        mb_unlock(mb);
        rt_trap("rt_msgbus: memory allocation failed");
        return 0;
    }

    int64_t count = 0;
    for (mb_sub *s = t->subs; s; s = s->next) {
        callbacks[count++] = s->callback;
        rt_obj_retain_maybe(s->callback);
    }
    mb_unlock(mb);

    int data_retained = mb_retain_managed_payload(data);
    volatile int64_t release_from = 0;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[512];
        const char *err = rt_trap_get_error();
        snprintf(saved_error,
                 sizeof(saved_error),
                 "%s",
                 err && err[0] ? err : "rt_msgbus_publish: subscriber trap");
        rt_trap_clear_recovery();
        for (int64_t i = (int64_t)release_from; i < count; ++i) {
            if (rt_obj_release_check0(callbacks[i]))
                rt_obj_free(callbacks[i]);
        }
        if (data_retained)
            (void)rt_memory_release(data);
        free(callbacks);
        rt_trap(saved_error);
        return 0;
    }
    for (int64_t i = 0; i < count; ++i) {
        mb_invoke_callback(callbacks[i], data);
        release_from = i + 1;
        if (rt_obj_release_check0(callbacks[i]))
            rt_obj_free(callbacks[i]);
    }
    rt_trap_clear_recovery();

    if (data_retained)
        (void)rt_memory_release(data);
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
    const char *topic_bytes = NULL;
    size_t topic_len = 0;
    if (!mb_topic_view(topic, &topic_bytes, &topic_len))
        return 0;
    mb_lock(mb);
    mb_topic *t = mb_find_topic_view_locked(mb, topic_bytes, topic_len);
    int64_t count = t ? t->count : 0;
    mb_unlock(mb);
    return count;
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
    if (!mb)
        return 0;
    mb_lock(mb);
    int64_t total = mb->total_subs;
    mb_unlock(mb);
    return total;
}

/// @brief Return a Seq containing the names of all topics that have at least one subscriber.
/// @param obj MessageBus object pointer; returns an empty Seq if NULL.
/// @return Seq of rt_string topic names (caller-owned via GC).
void *rt_msgbus_topics(void *obj) {
    void *seq = rt_seq_new();
    if (!seq)
        return NULL;
    rt_seq_set_owns_elements(seq, 1);
    if (!obj)
        return seq;
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_topics");
    if (!mb)
        return seq;

    int64_t count = 0;
    mb_lock(mb);
    for (int64_t i = 0; i < mb->bucket_count; i++) {
        mb_topic *t = mb->buckets[i];
        while (t) {
            if (t->count > 0)
                count++;
            t = t->next;
        }
    }
    mb_unlock(mb);

    if (count <= 0)
        return seq;

    rt_string *topics = (rt_string *)calloc((size_t)count, sizeof(rt_string));
    if (!topics) {
        if (rt_obj_release_check0(seq))
            rt_obj_free(seq);
        rt_trap("rt_msgbus: memory allocation failed");
        return NULL;
    }

    int64_t copied = 0;
    mb_lock(mb);
    for (int64_t i = 0; i < mb->bucket_count && copied < count; i++) {
        mb_topic *t = mb->buckets[i];
        while (t && copied < count) {
            if (t->count > 0) {
                topics[copied++] = rt_string_ref(t->name);
            }
            t = t->next;
        }
    }
    mb_unlock(mb);

    for (int64_t i = 0; i < copied; ++i) {
        rt_seq_push(seq, topics[i]);
        rt_string_unref(topics[i]);
    }
    free(topics);
    return seq;
}

/// @brief Remove all subscriptions from a single topic and remove its topic node.
/// @details Walks the subscriber linked list for the given topic, freeing each
///          node (releasing its topic string reference and callback).
///          The topic node is removed from the hash table when the last
///          subscriber is cleared, so Topics() never needs to skip stale
///          zero-count nodes.
/// @param obj MessageBus object pointer; no-op if NULL.
/// @param topic Topic name string to clear; no-op if NULL or not found.
void rt_msgbus_clear_topic(void *obj, rt_string topic) {
    if (!obj || !topic)
        return;
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_clear_topic");
    if (!mb)
        return;
    const char *topic_bytes = NULL;
    size_t topic_len = 0;
    if (!mb_topic_view(topic, &topic_bytes, &topic_len))
        return;
    mb_lock(mb);
    mb_topic *t = mb_find_topic_view_locked(mb, topic_bytes, topic_len);
    if (!t) {
        mb_unlock(mb);
        return;
    }

    mb_topic *removed = NULL;
    for (int64_t i = 0; i < mb->bucket_count && !removed; ++i) {
        mb_topic **tp = &mb->buckets[i];
        while (*tp) {
            if (*tp == t) {
                removed = t;
                *tp = t->next;
                t->next = NULL;
                break;
            }
            tp = &(*tp)->next;
        }
    }
    if (!removed) {
        mb_unlock(mb);
        rt_trap("rt_msgbus_clear_topic: topic unlink failed");
        return;
    }
    mb_sub *s = removed->subs;
    removed->subs = NULL;
    mb->total_subs -= removed->count;
    removed->count = 0;
    mb_unlock(mb);

    while (s) {
        mb_sub *next = s->next;
        mb_free_sub(s);
        s = next;
    }
    mb_free_topic_node(removed);
}

/// @brief Remove all subscriptions and topic nodes from the message bus.
/// @details Iterates all buckets and all topic chains, freeing every subscriber
///          node in each topic. The bucket array remains allocated so the bus is
///          reusable; topic nodes are recreated on the next subscription.
/// @param obj MessageBus object pointer; no-op if NULL.
void rt_msgbus_clear(void *obj) {
    if (!obj)
        return;
    rt_msgbus_impl *mb = mb_require(obj, "rt_msgbus_clear");
    if (!mb)
        return;

    mb_topic *topics_to_free = NULL;
    mb_lock(mb);
    for (int64_t i = 0; i < mb->bucket_count; i++) {
        mb_topic *t = mb->buckets[i];
        if (t) {
            mb_topic *tail = t;
            while (tail->next)
                tail = tail->next;
            tail->next = topics_to_free;
            topics_to_free = t;
            mb->buckets[i] = NULL;
        }
    }
    mb->total_subs = 0;
    mb_unlock(mb);

    mb_free_topic_chain(topics_to_free);
}
