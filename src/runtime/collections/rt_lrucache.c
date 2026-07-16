//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_lrucache.c
// Purpose: Implements a string-keyed Least Recently Used (LRU) cache combining
//   a hash table for O(1) key lookup with a doubly-linked list maintaining
//   access order. When the cache is full and a new entry is inserted, the least
//   recently accessed entry (tail of the list) is evicted automatically.
//
// Key invariants:
//   - Hash table has initial LRU_INITIAL_BUCKETS (16) buckets with separate
//     chaining; resizes at 75% load factor.
//   - LRU order is maintained as a doubly-linked list: head = MRU (most
//     recently used), tail = LRU (least recently used, next to evict).
//   - Get promotes the accessed node to the head of the list (O(1)).
//   - Put of an existing key updates the value and promotes to head (O(1)).
//   - Put of a new key when count == max_cap evicts the tail node before insert.
//   - max_cap is fixed at construction; a max_cap of 0 disables eviction (unbounded).
//   - Each node owns a heap-copied key string; values are stored as raw pointers
//     retained by the cache, and released on eviction/removal/finalization.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - LRUCache objects are GC-managed (rt_obj_new_i64). The bucket array and
//     all list nodes are freed by the GC finalizer (lrucache_finalizer).
//
// Links: src/runtime/collections/rt_lrucache.h (public API),
//        src/runtime/collections/rt_hash_util.h (FNV-1a hash macro)
//
//===----------------------------------------------------------------------===//

#include "rt_lrucache.h"

#include "rt_collection_ids.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <stdlib.h>
#include <string.h>

/// Initial number of hash table buckets.
#define LRU_INITIAL_BUCKETS 16

/// Load factor threshold for resizing (0.75 = 75%).
#define LRU_LOAD_FACTOR_NUM 3
#define LRU_LOAD_FACTOR_DEN 4

#include "rt_hash_util.h"

/// @brief Doubly-linked list node with hash table chaining.
typedef struct rt_lru_node {
    char *key;                       ///< Owned copy of key string (null-terminated).
    size_t key_len;                  ///< Length of key string (excluding null terminator).
    void *value;                     ///< Retained reference to the value object.
    struct rt_lru_node *prev;        ///< Previous node in LRU order (toward head/MRU).
    struct rt_lru_node *next;        ///< Next node in LRU order (toward tail/LRU).
    struct rt_lru_node *bucket_next; ///< Next node in hash bucket chain.
} rt_lru_node;

/// @brief LRU cache implementation structure.
typedef struct rt_lrucache_impl {
    void **vptr;           ///< Vtable pointer placeholder (for OOP compatibility).
    rt_lru_node **buckets; ///< Hash table buckets array.
    size_t bucket_count;   ///< Number of hash table buckets.
    size_t count;          ///< Current number of entries.
    size_t max_cap;        ///< Maximum number of entries before eviction.
    rt_lru_node *head;     ///< Most recently used node (doubly-linked list head).
    rt_lru_node *tail;     ///< Least recently used node (doubly-linked list tail).
} rt_lrucache_impl;

/// @brief Checked cast of an opaque handle to the LruCache implementation.
/// @details Traps with @p what if @p obj is NULL or not an LruCache.
static rt_lrucache_impl *as_lrucache(void *obj, const char *what) {
    if (!rt_obj_is_instance(obj, RT_LRUCACHE_CLASS_ID, sizeof(rt_lrucache_impl))) {
        rt_trap(what);
        return NULL;
    }
    return (rt_lrucache_impl *)obj;
}

/// @brief Borrow the byte buffer + length of a key string (empty "" if null).
static const char *get_key_data(rt_string key, size_t *out_len) {
    if (!key) {
        *out_len = 0;
        return "";
    }
    int64_t len = rt_str_len(key);
    if (len <= 0) {
        *out_len = 0;
        return "";
    }
    const char *cstr = rt_string_cstr(key);
    if (!cstr) {
        *out_len = 0;
        return "";
    }
    *out_len = (size_t)len;
    return cstr;
}

// ---------------------------------------------------------------------------
// Doubly-linked list helpers
// ---------------------------------------------------------------------------

/// Remove a node from the doubly-linked list (does NOT free it).
static void list_remove(rt_lrucache_impl *cache, rt_lru_node *node) {
    if (node->prev)
        node->prev->next = node->next;
    else
        cache->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        cache->tail = node->prev;

    node->prev = NULL;
    node->next = NULL;
}

/// Push a node to the front (MRU position) of the doubly-linked list.
static void list_push_front(rt_lrucache_impl *cache, rt_lru_node *node) {
    node->prev = NULL;
    node->next = cache->head;

    if (cache->head)
        cache->head->prev = node;
    else
        cache->tail = node; // List was empty

    cache->head = node;
}

/// Move an existing node to the front (MRU position).
static void list_move_to_front(rt_lrucache_impl *cache, rt_lru_node *node) {
    if (cache->head == node)
        return; // Already at front
    list_remove(cache, node);
    list_push_front(cache, node);
}

// ---------------------------------------------------------------------------
// Hash table helpers
// ---------------------------------------------------------------------------

/// Find a node in a bucket chain by key.
static rt_lru_node *bucket_find(rt_lru_node *head, const char *key, size_t key_len) {
    for (rt_lru_node *n = head; n; n = n->bucket_next) {
        if (n->key_len == key_len && memcmp(n->key, key, key_len) == 0)
            return n;
    }
    return NULL;
}

/// Remove a node from its bucket chain.
static void bucket_remove(rt_lrucache_impl *cache, rt_lru_node *node) {
    uint64_t hash = rt_fnv1a(node->key, node->key_len);
    size_t idx = hash % cache->bucket_count;

    rt_lru_node **prev_ptr = &cache->buckets[idx];
    rt_lru_node *curr = cache->buckets[idx];

    while (curr) {
        if (curr == node) {
            *prev_ptr = curr->bucket_next;
            curr->bucket_next = NULL;
            return;
        }
        prev_ptr = &curr->bucket_next;
        curr = curr->bucket_next;
    }
}

/// Insert a node into its bucket chain.
static void bucket_insert(rt_lrucache_impl *cache, rt_lru_node *node) {
    uint64_t hash = rt_fnv1a(node->key, node->key_len);
    size_t idx = hash % cache->bucket_count;
    node->bucket_next = cache->buckets[idx];
    cache->buckets[idx] = node;
}

/// Resize the hash table when load factor is too high.
static void resize_buckets(rt_lrucache_impl *cache) {
    if (cache->bucket_count > SIZE_MAX / 2)
        return; // Can't grow further
    size_t new_bucket_count = cache->bucket_count * 2;
    if (new_bucket_count > SIZE_MAX / sizeof(rt_lru_node *)) {
        rt_trap("LRUCache: allocation size overflow");
        return;
    }
    rt_lru_node **new_buckets = (rt_lru_node **)calloc(new_bucket_count, sizeof(rt_lru_node *));
    if (!new_buckets) {
        rt_trap("LRUCache: memory allocation failed");
        return;
    }

    // Rehash all nodes
    for (size_t i = 0; i < cache->bucket_count; ++i) {
        rt_lru_node *node = cache->buckets[i];
        while (node) {
            rt_lru_node *next = node->bucket_next;
            uint64_t hash = rt_fnv1a(node->key, node->key_len);
            size_t idx = hash % new_bucket_count;
            node->bucket_next = new_buckets[idx];
            new_buckets[idx] = node;
            node = next;
        }
    }

    free(cache->buckets);
    cache->buckets = new_buckets;
    cache->bucket_count = new_bucket_count;
}

static void maybe_resize_for_count(rt_lrucache_impl *cache, size_t next_count) {
    if ((long double)next_count * (long double)LRU_LOAD_FACTOR_DEN <=
        (long double)cache->bucket_count * (long double)LRU_LOAD_FACTOR_NUM)
        return;
    resize_buckets(cache);
}

// ---------------------------------------------------------------------------
// Node lifecycle
// ---------------------------------------------------------------------------

/// Free a node: release its key, value, and the node itself.
static void free_node(rt_lru_node *node) {
    if (!node)
        return;
    free(node->key);
    if (node->value && rt_obj_release_check0(node->value))
        rt_obj_free(node->value);
    free(node);
}

/// Evict the least-recently-used node (tail of the list).
static void evict_lru(rt_lrucache_impl *cache) {
    rt_lru_node *victim = cache->tail;
    if (!victim)
        return;

    list_remove(cache, victim);
    bucket_remove(cache, victim);
    cache->count--;
    free_node(victim);
}

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

/// @brief GC finalizer: free every node by walking the LRU list, then free
///        the bucket array.
static void rt_lrucache_finalize(void *obj) {
    if (!obj)
        return;
    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache: invalid LRUCache object");

    // Free all nodes via the linked list (faster than iterating buckets)
    rt_lru_node *node = cache->head;
    while (node) {
        rt_lru_node *next = node->next;
        free_node(node);
        node = next;
    }
    cache->head = NULL;
    cache->tail = NULL;
    cache->count = 0;

    free(cache->buckets);
    cache->buckets = NULL;
    cache->bucket_count = 0;
}

/// @brief GC traversal: visit every cached value across the node list.
static void rt_lrucache_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    if (!obj || !visitor)
        return;
    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache: invalid LRUCache object");
    for (rt_lru_node *node = cache->head; node; node = node->next)
        visitor(node->value, ctx);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void *rt_lrucache_new(int64_t capacity) {
    if (capacity < 0) {
        rt_trap("LRUCache: negative capacity");
        return NULL;
    }

    size_t bucket_count = LRU_INITIAL_BUCKETS;
    // If requested capacity is large, start with more buckets to avoid
    // immediate resizing
    while ((long double)bucket_count * (long double)LRU_LOAD_FACTOR_NUM /
               (long double)LRU_LOAD_FACTOR_DEN <
           (long double)capacity) {
        if (bucket_count > SIZE_MAX / 2)
            break;
        bucket_count *= 2;
    }

    if (bucket_count > SIZE_MAX / sizeof(rt_lru_node *)) {
        rt_trap("LRUCache: allocation size overflow");
        return NULL;
    }

    rt_lrucache_impl *cache =
        (rt_lrucache_impl *)rt_obj_new_i64(RT_LRUCACHE_CLASS_ID, (int64_t)sizeof(rt_lrucache_impl));
    if (!cache) {
        rt_trap("LRUCache: memory allocation failed");
        return NULL;
    }

    cache->vptr = NULL;
    cache->bucket_count = bucket_count;
    cache->buckets = (rt_lru_node **)calloc(cache->bucket_count, sizeof(rt_lru_node *));
    if (!cache->buckets) {
        if (rt_obj_release_check0(cache))
            rt_obj_free(cache);
        rt_trap("LRUCache: memory allocation failed");
        return NULL;
    }

    cache->count = 0;
    cache->max_cap = (size_t)capacity;
    cache->head = NULL;
    cache->tail = NULL;
    rt_obj_set_finalizer(cache, rt_lrucache_finalize);
    rt_gc_track(cache, rt_lrucache_traverse);
    return cache;
}

/// @brief Number of entries currently held by the cache (0..cap).
int64_t rt_lrucache_len(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)as_lrucache(obj, "LRUCache.Len: invalid LRUCache object")->count;
}

/// @brief Maximum capacity (set on construction). When `len() == cap()`, a `_put` evicts.
int64_t rt_lrucache_cap(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)as_lrucache(obj, "LRUCache.Cap: invalid LRUCache object")->max_cap;
}

/// @brief Returns 1 if the cache holds zero entries.
int8_t rt_lrucache_is_empty(void *obj) {
    return rt_lrucache_len(obj) == 0;
}

/// @brief Insert or update `key → value`. Existing keys have their value replaced and are
/// promoted to MRU. New insertions evict the LRU entry when at capacity. Both old and new
/// values are reference-counted (old released, new retained). Doubles bucket count when needed.
void rt_lrucache_put(void *obj, rt_string key, void *value) {
    if (!obj)
        return;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.Put: invalid LRUCache object");
    if (!cache)
        return;
    if (cache->bucket_count == 0)
        return;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % cache->bucket_count;

    // Check if key already exists
    rt_lru_node *existing = bucket_find(cache->buckets[idx], key_data, key_len);
    if (existing) {
        // Update value and promote to MRU
        void *old_value = existing->value;
        rt_obj_retain_maybe(value);
        existing->value = value;
        if (old_value && rt_obj_release_check0(old_value))
            rt_obj_free(old_value);
        list_move_to_front(cache, existing);
        return;
    }

    size_t projected_count =
        (cache->max_cap != 0 && cache->count >= cache->max_cap) ? cache->count : cache->count + 1;
    maybe_resize_for_count(cache, projected_count);

    if (value)
        rt_obj_retain_maybe(value);

    // Create new node
    rt_lru_node *node = (rt_lru_node *)malloc(sizeof(rt_lru_node));
    if (!node) {
        if (value && rt_obj_release_check0(value))
            rt_obj_free(value);
        rt_trap("LRUCache: memory allocation failed");
        return;
    }

    if (key_len == SIZE_MAX) {
        if (value && rt_obj_release_check0(value))
            rt_obj_free(value);
        free(node);
        rt_trap("LRUCache: key allocation overflow");
        return;
    }
    node->key = (char *)malloc(key_len + 1);
    if (!node->key) {
        if (value && rt_obj_release_check0(value))
            rt_obj_free(value);
        free(node);
        rt_trap("LRUCache: key allocation failed");
        return;
    }
    memcpy(node->key, key_data, key_len);
    node->key[key_len] = '\0';
    node->key_len = key_len;

    node->value = value;
    node->prev = NULL;
    node->next = NULL;
    node->bucket_next = NULL;

    // Evict only after all fallible preparation for the replacement entry succeeds.
    if (cache->max_cap != 0 && cache->count >= cache->max_cap)
        evict_lru(cache);

    // Insert into hash table and linked list
    if (cache->count >= (size_t)INT64_MAX) {
        if (node->value && rt_obj_release_check0(node->value))
            rt_obj_free(node->value);
        free(node->key);
        free(node);
        rt_trap("LRUCache.Put: maximum size reached");
        return;
    }
    bucket_insert(cache, node);
    list_push_front(cache, node);
    cache->count++;
}

/// @brief Look up `key` and promote it to MRU. Returns the borrowed value pointer or NULL if
/// absent. Caller must NOT keep the pointer past the next cache mutation (it may be evicted).
void *rt_lrucache_get(void *obj, rt_string key) {
    if (!obj)
        return NULL;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.Get: invalid LRUCache object");
    if (cache->bucket_count == 0)
        return NULL;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % cache->bucket_count;

    rt_lru_node *node = bucket_find(cache->buckets[idx], key_data, key_len);
    if (!node)
        return NULL;

    // Promote to MRU
    list_move_to_front(cache, node);
    // Returns borrowed reference — caller must not store past next cache mutation
    return node->value;
}

/// @brief Look up `key` *without* changing LRU order. Use for "is this cached?" probes that
/// shouldn't fight the eviction policy.
void *rt_lrucache_peek(void *obj, rt_string key) {
    if (!obj)
        return NULL;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.Peek: invalid LRUCache object");
    if (cache->bucket_count == 0)
        return NULL;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % cache->bucket_count;

    rt_lru_node *node = bucket_find(cache->buckets[idx], key_data, key_len);
    return node ? node->value : NULL;
}

/// @brief Returns 1 if `key` is currently cached. Does not change LRU order.
int8_t rt_lrucache_has(void *obj, rt_string key) {
    if (!obj)
        return 0;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.Has: invalid LRUCache object");
    if (cache->bucket_count == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % cache->bucket_count;

    return bucket_find(cache->buckets[idx], key_data, key_len) ? 1 : 0;
}

/// @brief Remove an entry by `key`. Releases the value's reference. Returns 1 on success,
/// 0 if the key wasn't present.
int8_t rt_lrucache_remove(void *obj, rt_string key) {
    if (!obj)
        return 0;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.Remove: invalid LRUCache object");
    if (cache->bucket_count == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % cache->bucket_count;

    rt_lru_node *node = bucket_find(cache->buckets[idx], key_data, key_len);
    if (!node)
        return 0;

    list_remove(cache, node);
    bucket_remove(cache, node);
    cache->count--;
    free_node(node);
    return 1;
}

/// @brief Force-evict the least-recently-used entry. Returns 1 if something was removed,
/// 0 if the cache was already empty.
int8_t rt_lrucache_remove_oldest(void *obj) {
    if (!obj)
        return 0;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.RemoveOldest: invalid LRUCache object");
    if (!cache->tail)
        return 0;

    evict_lru(cache);
    return 1;
}

/// @brief Remove every entry, releasing all values. Capacity and bucket count are preserved
/// — the cache is reusable immediately without reallocation.
void rt_lrucache_clear(void *obj) {
    if (!obj)
        return;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.Clear: invalid LRUCache object");

    // Free all nodes via the linked list
    rt_lru_node *node = cache->head;
    while (node) {
        rt_lru_node *next = node->next;
        free_node(node);
        node = next;
    }
    cache->head = NULL;
    cache->tail = NULL;
    cache->count = 0;

    // Clear bucket pointers
    if (cache->buckets)
        memset(cache->buckets, 0, cache->bucket_count * sizeof(rt_lru_node *));
}

/// @brief Return a Seq of all keys in MRU→LRU order. Owned-elements Seq (will release strings
/// on its own destruction). Snapshot — subsequent cache mutations don't affect the result.
void *rt_lrucache_keys(void *obj) {
    void *result = rt_seq_new();
    rt_seq_set_owns_elements(result, 1);
    if (!obj)
        return result;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.Keys: invalid LRUCache object");

    // Walk from head (MRU) to tail (LRU)
    for (rt_lru_node *node = cache->head; node; node = node->next) {
        rt_string key_str = rt_string_from_bytes(node->key, node->key_len);
        rt_seq_push(result, (void *)key_str);
        rt_str_release_maybe(key_str);
    }

    return result;
}

/// @brief Return an owning Seq of the values in MRU→LRU order (the snapshot
/// retains each entry and does not follow later cache mutations or evictions).
void *rt_lrucache_values(void *obj) {
    void *result = rt_seq_new();
    rt_seq_set_owns_elements(result, 1);
    if (!obj)
        return result;

    rt_lrucache_impl *cache = as_lrucache(obj, "LRUCache.Values: invalid LRUCache object");

    // Walk from head (MRU) to tail (LRU)
    for (rt_lru_node *node = cache->head; node; node = node->next) {
        rt_seq_push(result, node->value);
    }

    return result;
}
