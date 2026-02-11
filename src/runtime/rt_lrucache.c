//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_lrucache.c
// Purpose: Implement a string-keyed LRU cache using a hash table + doubly
//          linked list. O(1) get/put/remove with automatic eviction of least
//          recently used entries when capacity is exceeded.
// Structure: [vptr | buckets | capacity | count | max_cap | head | tail]
//
//===----------------------------------------------------------------------===//

#include "rt_lrucache.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// Initial number of hash table buckets.
#define LRU_INITIAL_BUCKETS 16

/// Load factor threshold for resizing (0.75 = 75%).
#define LRU_LOAD_FACTOR_NUM 3
#define LRU_LOAD_FACTOR_DEN 4

#include "rt_hash_util.h"

/// @brief Doubly-linked list node with hash table chaining.
typedef struct rt_lru_node
{
    char *key;                       ///< Owned copy of key string (null-terminated).
    size_t key_len;                  ///< Length of key string (excluding null terminator).
    void *value;                     ///< Retained reference to the value object.
    struct rt_lru_node *prev;        ///< Previous node in LRU order (toward head/MRU).
    struct rt_lru_node *next;        ///< Next node in LRU order (toward tail/LRU).
    struct rt_lru_node *bucket_next; ///< Next node in hash bucket chain.
} rt_lru_node;

/// @brief LRU cache implementation structure.
typedef struct rt_lrucache_impl
{
    void **vptr;           ///< Vtable pointer placeholder (for OOP compatibility).
    rt_lru_node **buckets; ///< Hash table buckets array.
    size_t bucket_count;   ///< Number of hash table buckets.
    size_t count;          ///< Current number of entries.
    size_t max_cap;        ///< Maximum number of entries before eviction.
    rt_lru_node *head;     ///< Most recently used node (doubly-linked list head).
    rt_lru_node *tail;     ///< Least recently used node (doubly-linked list tail).
} rt_lrucache_impl;

static const char *get_key_data(rt_string key, size_t *out_len)
{
    const char *cstr = rt_string_cstr(key);
    if (!cstr)
    {
        *out_len = 0;
        return "";
    }
    *out_len = strlen(cstr);
    return cstr;
}

// ---------------------------------------------------------------------------
// Doubly-linked list helpers
// ---------------------------------------------------------------------------

/// Remove a node from the doubly-linked list (does NOT free it).
static void list_remove(rt_lrucache_impl *cache, rt_lru_node *node)
{
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
static void list_push_front(rt_lrucache_impl *cache, rt_lru_node *node)
{
    node->prev = NULL;
    node->next = cache->head;

    if (cache->head)
        cache->head->prev = node;
    else
        cache->tail = node; // List was empty

    cache->head = node;
}

/// Move an existing node to the front (MRU position).
static void list_move_to_front(rt_lrucache_impl *cache, rt_lru_node *node)
{
    if (cache->head == node)
        return; // Already at front
    list_remove(cache, node);
    list_push_front(cache, node);
}

// ---------------------------------------------------------------------------
// Hash table helpers
// ---------------------------------------------------------------------------

/// Find a node in a bucket chain by key.
static rt_lru_node *bucket_find(rt_lru_node *head, const char *key, size_t key_len)
{
    for (rt_lru_node *n = head; n; n = n->bucket_next)
    {
        if (n->key_len == key_len && memcmp(n->key, key, key_len) == 0)
            return n;
    }
    return NULL;
}

/// Remove a node from its bucket chain.
static void bucket_remove(rt_lrucache_impl *cache, rt_lru_node *node)
{
    uint64_t hash = rt_fnv1a(node->key, node->key_len);
    size_t idx = hash % cache->bucket_count;

    rt_lru_node **prev_ptr = &cache->buckets[idx];
    rt_lru_node *curr = cache->buckets[idx];

    while (curr)
    {
        if (curr == node)
        {
            *prev_ptr = curr->bucket_next;
            curr->bucket_next = NULL;
            return;
        }
        prev_ptr = &curr->bucket_next;
        curr = curr->bucket_next;
    }
}

/// Insert a node into its bucket chain.
static void bucket_insert(rt_lrucache_impl *cache, rt_lru_node *node)
{
    uint64_t hash = rt_fnv1a(node->key, node->key_len);
    size_t idx = hash % cache->bucket_count;
    node->bucket_next = cache->buckets[idx];
    cache->buckets[idx] = node;
}

/// Resize the hash table when load factor is too high.
static void maybe_resize(rt_lrucache_impl *cache)
{
    if (cache->count * LRU_LOAD_FACTOR_DEN <= cache->bucket_count * LRU_LOAD_FACTOR_NUM)
        return;

    size_t new_bucket_count = cache->bucket_count * 2;
    rt_lru_node **new_buckets = (rt_lru_node **)calloc(new_bucket_count, sizeof(rt_lru_node *));
    if (!new_buckets)
        return; // Keep old buckets on failure

    // Rehash all nodes
    for (size_t i = 0; i < cache->bucket_count; ++i)
    {
        rt_lru_node *node = cache->buckets[i];
        while (node)
        {
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

// ---------------------------------------------------------------------------
// Node lifecycle
// ---------------------------------------------------------------------------

/// Free a node: release its key, value, and the node itself.
static void free_node(rt_lru_node *node)
{
    if (!node)
        return;
    free(node->key);
    if (node->value && rt_obj_release_check0(node->value))
        rt_obj_free(node->value);
    free(node);
}

/// Evict the least-recently-used node (tail of the list).
static void evict_lru(rt_lrucache_impl *cache)
{
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

static void rt_lrucache_finalize(void *obj)
{
    if (!obj)
        return;
    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;

    // Free all nodes via the linked list (faster than iterating buckets)
    rt_lru_node *node = cache->head;
    while (node)
    {
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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void *rt_lrucache_new(int64_t capacity)
{
    if (capacity <= 0)
        capacity = 1; // Minimum capacity of 1

    rt_lrucache_impl *cache =
        (rt_lrucache_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_lrucache_impl));
    if (!cache)
        return NULL;

    cache->vptr = NULL;
    cache->bucket_count = LRU_INITIAL_BUCKETS;
    // If requested capacity is large, start with more buckets to avoid
    // immediate resizing
    while (cache->bucket_count * LRU_LOAD_FACTOR_NUM / LRU_LOAD_FACTOR_DEN < (size_t)capacity)
    {
        cache->bucket_count *= 2;
    }

    cache->buckets = (rt_lru_node **)calloc(cache->bucket_count, sizeof(rt_lru_node *));
    if (!cache->buckets)
    {
        cache->bucket_count = 0;
        cache->count = 0;
        cache->max_cap = 0;
        cache->head = NULL;
        cache->tail = NULL;
        rt_obj_set_finalizer(cache, rt_lrucache_finalize);
        return cache;
    }

    cache->count = 0;
    cache->max_cap = (size_t)capacity;
    cache->head = NULL;
    cache->tail = NULL;
    rt_obj_set_finalizer(cache, rt_lrucache_finalize);
    return cache;
}

int64_t rt_lrucache_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_lrucache_impl *)obj)->count;
}

int64_t rt_lrucache_cap(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_lrucache_impl *)obj)->max_cap;
}

int8_t rt_lrucache_is_empty(void *obj)
{
    return rt_lrucache_len(obj) == 0;
}

void rt_lrucache_put(void *obj, rt_string key, void *value)
{
    if (!obj)
        return;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;
    if (cache->bucket_count == 0)
        return;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % cache->bucket_count;

    // Check if key already exists
    rt_lru_node *existing = bucket_find(cache->buckets[idx], key_data, key_len);
    if (existing)
    {
        // Update value and promote to MRU
        void *old_value = existing->value;
        rt_obj_retain_maybe(value);
        existing->value = value;
        if (old_value && rt_obj_release_check0(old_value))
            rt_obj_free(old_value);
        list_move_to_front(cache, existing);
        return;
    }

    // Evict LRU entry if at capacity
    if (cache->count >= cache->max_cap)
        evict_lru(cache);

    // Create new node
    rt_lru_node *node = (rt_lru_node *)malloc(sizeof(rt_lru_node));
    if (!node)
        return;

    node->key = (char *)malloc(key_len + 1);
    if (!node->key)
    {
        free(node);
        return;
    }
    memcpy(node->key, key_data, key_len);
    node->key[key_len] = '\0';
    node->key_len = key_len;

    rt_obj_retain_maybe(value);
    node->value = value;
    node->prev = NULL;
    node->next = NULL;
    node->bucket_next = NULL;

    // Insert into hash table and linked list
    bucket_insert(cache, node);
    list_push_front(cache, node);
    cache->count++;

    maybe_resize(cache);
}

void *rt_lrucache_get(void *obj, rt_string key)
{
    if (!obj)
        return NULL;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;
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
    return node->value;
}

void *rt_lrucache_peek(void *obj, rt_string key)
{
    if (!obj)
        return NULL;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;
    if (cache->bucket_count == 0)
        return NULL;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % cache->bucket_count;

    rt_lru_node *node = bucket_find(cache->buckets[idx], key_data, key_len);
    return node ? node->value : NULL;
}

int8_t rt_lrucache_has(void *obj, rt_string key)
{
    if (!obj)
        return 0;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;
    if (cache->bucket_count == 0)
        return 0;

    size_t key_len;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t hash = rt_fnv1a(key_data, key_len);
    size_t idx = hash % cache->bucket_count;

    return bucket_find(cache->buckets[idx], key_data, key_len) ? 1 : 0;
}

int8_t rt_lrucache_remove(void *obj, rt_string key)
{
    if (!obj)
        return 0;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;
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

int8_t rt_lrucache_remove_oldest(void *obj)
{
    if (!obj)
        return 0;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;
    if (!cache->tail)
        return 0;

    evict_lru(cache);
    return 1;
}

void rt_lrucache_clear(void *obj)
{
    if (!obj)
        return;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;

    // Free all nodes via the linked list
    rt_lru_node *node = cache->head;
    while (node)
    {
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

void *rt_lrucache_keys(void *obj)
{
    void *result = rt_seq_new();
    if (!obj)
        return result;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;

    // Walk from head (MRU) to tail (LRU)
    for (rt_lru_node *node = cache->head; node; node = node->next)
    {
        rt_string key_str = rt_string_from_bytes(node->key, node->key_len);
        rt_seq_push(result, (void *)key_str);
    }

    return result;
}

void *rt_lrucache_values(void *obj)
{
    void *result = rt_seq_new();
    if (!obj)
        return result;

    rt_lrucache_impl *cache = (rt_lrucache_impl *)obj;

    // Walk from head (MRU) to tail (LRU)
    for (rt_lru_node *node = cache->head; node; node = node->next)
    {
        rt_seq_push(result, node->value);
    }

    return result;
}
