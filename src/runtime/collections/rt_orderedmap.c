//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_orderedmap.c
// Purpose: Implements an insertion-ordered string-keyed map that preserves the
//   order in which keys were first inserted. Combines a hash table for O(1)
//   key lookup with a doubly-linked list maintaining insertion order. Iteration
//   always visits entries in the order they were inserted, not hash order.
//
// Key invariants:
//   - Hash table starts at capacity 16 and resizes (doubles) at 75% load.
//   - Each entry node belongs to both a hash bucket chain (hash_next) and the
//     insertion-order doubly-linked list (prev/next). head = first inserted,
//     tail = last inserted.
//   - Updating an existing key's value preserves its position in the insertion
//     order; the node is not moved to the tail.
//   - Removing an entry unlinks it from both the bucket chain and the list.
//   - Key strings are heap-copied into entry nodes; the OrderedMap is
//     independent of the source rt_string lifetime.
//   - Values are retained on insertion and released on removal/finalization.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - OrderedMap objects are GC-managed (rt_obj_new_i64). The bucket array and
//     all entry nodes are freed by the GC finalizer (orderedmap_finalizer).
//
// Links: src/runtime/collections/rt_orderedmap.h (public API),
//        src/runtime/collections/rt_map.h (unordered map counterpart)
//
//===----------------------------------------------------------------------===//

#include "rt_orderedmap.h"
#include "rt_collection_ids.h"
#include "rt_error.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal structure: doubly-linked list + hash table
// ---------------------------------------------------------------------------

typedef struct rt_om_entry {
    char *key;
    size_t key_len;
    void *value;
    struct rt_om_entry *hash_next; // Hash chain
    struct rt_om_entry *prev;      // Insertion order
    struct rt_om_entry *next;      // Insertion order
} rt_om_entry;

typedef struct {
    void *vptr;
    rt_om_entry **buckets;
    int64_t capacity;
    int64_t count;
    rt_om_entry *head; // First inserted
    rt_om_entry *tail; // Last inserted
} rt_orderedmap_impl;

/// @brief Checked cast of an opaque handle to the OrderedMap implementation.
/// @details Raises a runtime-error trap with @p what if @p obj is NULL or
///          not an OrderedMap.
static rt_orderedmap_impl *as_orderedmap(void *obj, const char *what) {
    if (!obj || rt_obj_class_id(obj) != RT_ORDEREDMAP_CLASS_ID) {
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR, Err_RuntimeError, -1, what);
        return NULL;
    }
    return (rt_orderedmap_impl *)obj;
}

// ---------------------------------------------------------------------------
// Hash helpers
// ---------------------------------------------------------------------------

/// @brief FNV-1a 64-bit hash of @p len bytes of @p key.
static uint64_t om_hash(const char *key, size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)(unsigned char)key[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/// @brief Borrow the byte buffer + length of a key string (empty "" if null).
static const char *om_key_data(rt_string key, size_t *out_len) {
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

/// @brief Drop one GC reference to a stored value and free it at zero.
static void om_release_value(void *value) {
    if (value && rt_obj_release_check0(value))
        rt_obj_free(value);
}

/// @brief Hash-bucket lookup of @p key via the hash_next chain (NULL if absent).
static rt_om_entry *om_find(rt_orderedmap_impl *m, const char *key, size_t len) {
    uint64_t idx = om_hash(key, len) % (uint64_t)m->capacity;
    rt_om_entry *e = m->buckets[idx];
    while (e) {
        if (e->key_len == len && memcmp(e->key, key, len) == 0)
            return e;
        e = e->hash_next;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

/// @brief Double the bucket array and rehash entries, preserving insertion
///        order by re-linking via the head→next list. Traps on overflow/OOM.
static int om_resize(rt_orderedmap_impl *m) {
    // Guard against integer overflow before doubling.
    if (m->capacity > INT64_MAX / 2) {
        rt_trap_raise_kind(
            RT_TRAP_KIND_OVERFLOW, Err_Overflow, -1, "OrderedMap: capacity overflow during resize");
        return 0;
    }

    int64_t new_cap = m->capacity * 2;
    if ((uint64_t)new_cap > SIZE_MAX / sizeof(rt_om_entry *)) {
        rt_trap_raise_kind(RT_TRAP_KIND_OVERFLOW,
                           Err_Overflow,
                           -1,
                           "OrderedMap: allocation size overflow during resize");
        return 0;
    }
    rt_om_entry **new_buckets = (rt_om_entry **)calloc((size_t)new_cap, sizeof(rt_om_entry *));
    if (!new_buckets) {
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "OrderedMap: memory allocation failed during resize");
        return 0;
    }

    // Re-hash all entries via insertion-order list
    rt_om_entry *e = m->head;
    while (e) {
        uint64_t idx = om_hash(e->key, e->key_len) % (uint64_t)new_cap;
        e->hash_next = new_buckets[idx];
        new_buckets[idx] = e;
        e = e->next;
    }

    free(m->buckets);
    m->buckets = new_buckets;
    m->capacity = new_cap;
    return 1;
}

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

/// @brief GC finalizer: walk the insertion-order list freeing each entry
///        (key + released value), then free the bucket array.
static void orderedmap_finalizer(void *obj) {
    if (!obj)
        return;
    rt_orderedmap_impl *m = as_orderedmap(obj, "OrderedMap: invalid OrderedMap object");
    if (!m)
        return;
    rt_om_entry *e = m->head;
    while (e) {
        rt_om_entry *next = e->next;
        free(e->key);
        om_release_value(e->value);
        free(e);
        e = next;
    }
    free(m->buckets);
    m->buckets = NULL;
    m->head = m->tail = NULL;
    m->capacity = 0;
    m->count = 0;
}

/// @brief GC traversal: visit every stored value in insertion order.
static void orderedmap_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    if (!obj || !visitor)
        return;
    rt_orderedmap_impl *m = as_orderedmap(obj, "OrderedMap: invalid OrderedMap object");
    if (!m)
        return;
    for (rt_om_entry *e = m->head; e; e = e->next)
        visitor(e->value, ctx);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

void *rt_orderedmap_new(void) {
    rt_orderedmap_impl *m =
        (rt_orderedmap_impl *)rt_obj_new_i64(RT_ORDEREDMAP_CLASS_ID, sizeof(rt_orderedmap_impl));
    if (!m) {
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "OrderedMap: memory allocation failed");
        return NULL;
    }
    m->capacity = 16;
    m->count = 0;
    m->buckets = (rt_om_entry **)calloc(16, sizeof(rt_om_entry *));
    if (!m->buckets) {
        if (rt_obj_release_check0(m))
            rt_obj_free(m);
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "OrderedMap: memory allocation failed");
        return NULL;
    }
    m->head = m->tail = NULL;
    rt_obj_set_finalizer(m, orderedmap_finalizer);
    rt_gc_track(m, orderedmap_traverse);
    return m;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/// @brief Return the number of entries in the ordered map.
int64_t rt_orderedmap_len(void *map) {
    if (!map)
        return 0;
    return as_orderedmap(map, "OrderedMap.Len: invalid OrderedMap object")->count;
}

/// @brief Check whether the ordered map has no entries.
int64_t rt_orderedmap_is_empty(void *map) {
    if (!map)
        return 1;
    return as_orderedmap(map, "OrderedMap.IsEmpty: invalid OrderedMap object")->count == 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Set
// ---------------------------------------------------------------------------

/// @brief Insert or update a key-value pair, preserving insertion order.
/// @details New keys are appended to the end of the order. Updating an
///          existing key replaces the value but keeps its position.
void rt_orderedmap_set(void *map, rt_string key, void *value) {
    if (!map)
        return;
    rt_orderedmap_impl *m = as_orderedmap(map, "OrderedMap.Set: invalid OrderedMap object");
    if (!m)
        return;

    size_t klen;
    const char *kstr = om_key_data(key, &klen);

    // Check for existing key
    rt_om_entry *existing = om_find(m, kstr, klen);
    if (existing) {
        // Update value in-place (preserves order)
        if (value)
            rt_obj_retain_maybe(value);
        om_release_value(existing->value);
        existing->value = value;
        return;
    }

    // Resize if needed
    if ((long double)m->count * 4.0L >= (long double)m->capacity * 3.0L && !om_resize(m))
        return;

    if (value)
        rt_obj_retain_maybe(value);

    // Create new entry
    rt_om_entry *e = (rt_om_entry *)calloc(1, sizeof(rt_om_entry));
    if (!e) {
        om_release_value(value);
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "OrderedMap: entry allocation failed");
        return;
    }
    if (klen == SIZE_MAX) {
        om_release_value(value);
        free(e);
        rt_trap_raise_kind(
            RT_TRAP_KIND_OVERFLOW, Err_Overflow, -1, "OrderedMap: key allocation overflow");
        return;
    }
    e->key = (char *)malloc(klen + 1);
    if (!e->key) {
        om_release_value(value);
        free(e);
        rt_trap_raise_kind(
            RT_TRAP_KIND_RUNTIME_ERROR, Err_RuntimeError, -1, "OrderedMap: key allocation failed");
        return;
    }
    memcpy(e->key, kstr, klen);
    e->key[klen] = '\0';
    e->key_len = klen;
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

void *rt_orderedmap_get(void *map, rt_string key) {
    if (!map)
        return NULL;
    rt_orderedmap_impl *m = as_orderedmap(map, "OrderedMap.Get: invalid OrderedMap object");

    size_t klen;
    const char *kstr = om_key_data(key, &klen);

    rt_om_entry *e = om_find(m, kstr, klen);
    return e ? e->value : NULL;
}

/// @brief Check whether a key exists in the ordered map.
int64_t rt_orderedmap_has(void *map, rt_string key) {
    if (!map)
        return 0;
    rt_orderedmap_impl *m = as_orderedmap(map, "OrderedMap.Has: invalid OrderedMap object");

    size_t klen;
    const char *kstr = om_key_data(key, &klen);

    return om_find(m, kstr, klen) != NULL ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Remove
// ---------------------------------------------------------------------------

/// @brief Remove a key-value pair from the ordered map.
/// @details The entry is removed from both the hash table and the
///          insertion-order linked list.
int8_t rt_orderedmap_remove(void *map, rt_string key) {
    if (!map)
        return 0;
    rt_orderedmap_impl *m = as_orderedmap(map, "OrderedMap.Remove: invalid OrderedMap object");

    size_t klen;
    const char *kstr = om_key_data(key, &klen);

    uint64_t idx = om_hash(kstr, klen) % (uint64_t)m->capacity;

    // Remove from hash chain
    rt_om_entry **pp = &m->buckets[idx];
    while (*pp) {
        rt_om_entry *e = *pp;
        if (e->key_len == klen && memcmp(e->key, kstr, klen) == 0) {
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
            om_release_value(e->value);
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

void *rt_orderedmap_keys(void *map) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!map)
        return seq;
    rt_orderedmap_impl *m = as_orderedmap(map, "OrderedMap.Keys: invalid OrderedMap object");

    rt_om_entry *e = m->head;
    while (e) {
        rt_string k = rt_string_from_bytes(e->key, e->key_len);
        rt_seq_push(seq, k);
        rt_str_release_maybe(k);
        e = e->next;
    }
    return seq;
}

void *rt_orderedmap_values(void *map) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!map)
        return seq;
    rt_orderedmap_impl *m = as_orderedmap(map, "OrderedMap.Values: invalid OrderedMap object");

    rt_om_entry *e = m->head;
    while (e) {
        if (e->value)
            rt_seq_push(seq, e->value);
        else
            rt_seq_push(seq, NULL);
        e = e->next;
    }
    return seq;
}

/// @brief Return the key at the given insertion-order index.
/// @details Walks the insertion-order linked list to the nth entry.
/// @param map Ordered map object pointer; returns NULL if NULL.
/// @param index Zero-based position in insertion order.
/// @return Key string at the given position, or NULL if out of range.
rt_string rt_orderedmap_key_at(void *map, int64_t index) {
    if (!map)
        return NULL;
    rt_orderedmap_impl *m = as_orderedmap(map, "OrderedMap.KeyAt: invalid OrderedMap object");

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

/// @brief Remove all entries from the ordered map.
/// @details Releases all retained references and resets both the hash
///          table and the insertion-order list.
void rt_orderedmap_clear(void *map) {
    if (!map)
        return;
    rt_orderedmap_impl *m = as_orderedmap(map, "OrderedMap.Clear: invalid OrderedMap object");

    rt_om_entry *e = m->head;
    while (e) {
        rt_om_entry *next = e->next;
        free(e->key);
        om_release_value(e->value);
        free(e);
        e = next;
    }

    memset(m->buckets, 0, (size_t)m->capacity * sizeof(rt_om_entry *));
    m->head = m->tail = NULL;
    m->count = 0;
}
