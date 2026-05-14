//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_frozenmap.c
// Purpose: Implements an immutable string-keyed map (FrozenMap) built once from
//   a Seq of alternating key/value pairs or from a parallel keys/values Seq.
//   After construction the map cannot be modified; all mutating operations are
//   absent from the API. Uses open-addressing with FNV-1a hashing for O(1)
//   average-case lookup.
//
// Key invariants:
//   - Open-addressing hash table; load factor is kept below 50% by sizing the
//     slot array to 2× the number of entries at construction time.
//   - Slot key == NULL indicates an empty slot (tombstones are not used since
//     the map is immutable after build).
//   - FNV-1a hash over the raw string bytes; linear probing on collision.
//   - Keys are stored as retained rt_string references (not copied); the FrozenMap
//     keeps a reference to prevent GC collection of key strings.
//   - Values are retained on insertion and released by the finalizer.
//   - Not thread-safe for construction; safe for concurrent read-only access
//     after construction completes.
//
// Ownership/Lifetime:
//   - FrozenMap objects are GC-managed (rt_obj_new_i64). The slots array is
//     freed by the GC finalizer (frozenmap_finalizer).
//
// Links: src/runtime/collections/rt_frozenmap.h (public API),
//        src/runtime/collections/rt_map.h (mutable map counterpart)
//
//===----------------------------------------------------------------------===//

#include "rt_frozenmap.h"

#include "rt_box.h"
#include "rt_collection_ids.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

// --- Helper: extract string from seq element (may be boxed) ---

static rt_string fm_extract_str(void *elem, int *owned) {
    if (owned)
        *owned = 0;
    if (!elem)
        return NULL;
    if (rt_string_is_handle(elem))
        return (rt_string)elem;
    // Not a raw string -- assume boxed value and unbox.
    if (owned)
        *owned = 1;
    return rt_unbox_str(elem);
}

// --- Hash table entry (open addressing) ---

typedef struct {
    rt_string key; // NULL = empty slot
    void *value;
} fm_slot;

typedef struct {
    void *vptr;
    int64_t count;
    int64_t capacity;
    fm_slot *slots;
} rt_frozenmap_impl;

static rt_frozenmap_impl *as_frozenmap(void *obj, const char *what) {
    if (!obj || rt_obj_class_id(obj) != RT_FROZENMAP_CLASS_ID)
        rt_trap(what);
    return (rt_frozenmap_impl *)obj;
}

// --- FNV-1a hash ---

static uint64_t fm_hash(const char *data, int64_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (int64_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t fm_str_hash(rt_string s) {
    if (!s)
        return fm_hash("", 0);
    int64_t len = rt_str_len(s);
    const char *cstr = rt_string_cstr(s);
    return fm_hash(cstr ? cstr : "", len > 0 && cstr ? len : 0);
}

// --- Internal helpers ---

static const char *fm_key_data(rt_string key, int64_t *out_len) {
    if (!key) {
        *out_len = 0;
        return "";
    }
    int64_t len = rt_str_len(key);
    if (len <= 0) {
        *out_len = 0;
        return "";
    }
    const char *data = rt_string_cstr(key);
    if (!data) {
        *out_len = 0;
        return "";
    }
    *out_len = len;
    return data;
}

static int8_t fm_key_equals(rt_string key, const char *data, int64_t len) {
    int64_t key_len = 0;
    const char *key_data = fm_key_data(key, &key_len);
    return key_len == len && memcmp(key_data, data, (size_t)len) == 0 ? 1 : 0;
}

static void fm_release_value(void *value) {
    if (value && rt_obj_release_check0(value))
        rt_obj_free(value);
}

static void fm_finalizer(void *obj) {
    if (!obj)
        return;
    rt_frozenmap_impl *fm = as_frozenmap(obj, "FrozenMap: invalid FrozenMap object");
    if (fm->slots) {
        for (int64_t i = 0; i < fm->capacity; i++) {
            if (fm->slots[i].key) {
                rt_string_unref(fm->slots[i].key);
                fm_release_value(fm->slots[i].value);
            }
        }
        free(fm->slots);
        fm->slots = NULL;
    }
}

static void fm_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    if (!obj || !visitor)
        return;
    rt_frozenmap_impl *fm = as_frozenmap(obj, "FrozenMap: invalid FrozenMap object");
    if (!fm->slots)
        return;
    for (int64_t i = 0; i < fm->capacity; i++) {
        if (fm->slots[i].key)
            visitor(fm->slots[i].value, ctx);
    }
}

static int64_t fm_next_pow2(int64_t n) {
    int64_t p = 16;
    while (p < n) {
        if (p > INT64_MAX / 2)
            rt_trap("FrozenMap: capacity overflow");
        p *= 2;
    }
    return p;
}

static rt_frozenmap_impl *fm_alloc(int64_t count) {
    int64_t needed = 8;
    if (count >= 4) {
        if (count > INT64_MAX / 2)
            rt_trap("FrozenMap: capacity overflow");
        needed = count * 2;
    }
    int64_t cap = fm_next_pow2(needed);
    rt_frozenmap_impl *fm =
        (rt_frozenmap_impl *)rt_obj_new_i64(RT_FROZENMAP_CLASS_ID, sizeof(rt_frozenmap_impl));
    if (!fm)
        rt_trap("FrozenMap: memory allocation failed");
    fm->count = 0;
    fm->capacity = cap;
    if ((uint64_t)cap > SIZE_MAX / sizeof(fm_slot))
        rt_trap("FrozenMap: allocation size overflow");
    fm->slots = (fm_slot *)calloc((size_t)cap, sizeof(fm_slot));
    if (!fm->slots) {
        if (rt_obj_release_check0(fm))
            rt_obj_free(fm);
        rt_trap("rt_frozenmap: memory allocation failed");
    }
    rt_obj_set_finalizer(fm, fm_finalizer);
    rt_gc_track(fm, fm_traverse);
    return fm;
}

// Insert or update. Returns 1 if new entry, 0 if updated.
static int8_t fm_insert(rt_frozenmap_impl *fm, rt_string key, void *value) {
    uint64_t h = fm_str_hash(key);
    int64_t mask = fm->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);
    int64_t key_len = 0;
    const char *key_data = fm_key_data(key, &key_len);

    for (int64_t i = 0; i < fm->capacity; i++) {
        int64_t slot = (idx + i) & mask;
        if (!fm->slots[slot].key) {
            fm->slots[slot].key = key;
            rt_obj_retain_maybe(key);
            fm->slots[slot].value = value;
            rt_obj_retain_maybe(value);
            fm->count++;
            return 1;
        }
        if (fm_key_equals(fm->slots[slot].key, key_data, key_len)) {
            // Update value (last writer wins)
            rt_obj_retain_maybe(value);
            fm_release_value(fm->slots[slot].value);
            fm->slots[slot].value = value;
            return 0;
        }
    }
    return 0;
}

static fm_slot *fm_find(rt_frozenmap_impl *fm, rt_string key) {
    if (!fm || fm->count == 0)
        return NULL;
    uint64_t h = fm_str_hash(key);
    int64_t mask = fm->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);
    int64_t key_len = 0;
    const char *key_data = fm_key_data(key, &key_len);

    for (int64_t i = 0; i < fm->capacity; i++) {
        int64_t slot = (idx + i) & mask;
        if (!fm->slots[slot].key)
            return NULL;
        if (fm_key_equals(fm->slots[slot].key, key_data, key_len))
            return &fm->slots[slot];
    }
    return NULL;
}

// --- Public API ---

/// @brief Build an immutable map from parallel `keys` / `values` Seqs (zips them by index).
/// Truncates to min(len(keys), len(values)). Internal storage is an open-addressed hash table
/// sized for the entry count. The result cannot be mutated — use `Map` for mutable maps.
void *rt_frozenmap_from_seqs(void *keys, void *values) {
    if (!keys || !values)
        return (void *)fm_alloc(0);

    int64_t nk = rt_seq_len(keys);
    int64_t nv = rt_seq_len(values);
    int64_t n = nk < nv ? nk : nv;

    rt_frozenmap_impl *fm = fm_alloc(n);

    for (int64_t i = 0; i < n; i++) {
        int owned_key = 0;
        rt_string k = fm_extract_str(rt_seq_get(keys, i), &owned_key);
        void *v = rt_seq_get(values, i);
        if (k)
            fm_insert(fm, k, v);
        if (owned_key)
            rt_str_release_maybe(k);
    }
    return (void *)fm;
}

/// @brief Construct an empty frozen map.
void *rt_frozenmap_empty(void) {
    return (void *)fm_alloc(0);
}

/// @brief Return the number of entries in the frozen (immutable) map.
int64_t rt_frozenmap_len(void *obj) {
    if (!obj)
        return 0;
    return as_frozenmap(obj, "FrozenMap: invalid FrozenMap object")->count;
}

/// @brief Check whether the frozen map has no entries.
int8_t rt_frozenmap_is_empty(void *obj) {
    return rt_frozenmap_len(obj) == 0 ? 1 : 0;
}

/// @brief Look up `key`. Returns the borrowed value or NULL if absent. O(1) average via hash.
void *rt_frozenmap_get(void *obj, rt_string key) {
    if (!obj || !key)
        return NULL;
    fm_slot *s = fm_find(as_frozenmap(obj, "FrozenMap: invalid FrozenMap object"), key);
    return s ? s->value : NULL;
}

/// @brief Check whether a key exists in the frozen map.
/// @details Uses hash-based lookup on the immutable backing array.
int8_t rt_frozenmap_has(void *obj, rt_string key) {
    if (!obj || !key)
        return 0;
    return fm_find(as_frozenmap(obj, "FrozenMap: invalid FrozenMap object"), key) != NULL ? 1 : 0;
}

/// @brief Return a Seq of every key in the map (slot-iteration order, not insertion order).
void *rt_frozenmap_keys(void *obj) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!obj)
        return seq;

    rt_frozenmap_impl *fm = as_frozenmap(obj, "FrozenMap: invalid FrozenMap object");
    for (int64_t i = 0; i < fm->capacity; i++) {
        if (fm->slots[i].key)
            rt_seq_push(seq, fm->slots[i].key);
    }
    return seq;
}

/// @brief Return a Seq of every value in the map (parallel order to `_keys`).
void *rt_frozenmap_values(void *obj) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!obj)
        return seq;

    rt_frozenmap_impl *fm = as_frozenmap(obj, "FrozenMap: invalid FrozenMap object");
    for (int64_t i = 0; i < fm->capacity; i++) {
        if (fm->slots[i].key)
            rt_seq_push(seq, fm->slots[i].value);
    }
    return seq;
}

/// @brief Look up `key`, returning `default_value` if absent. Lets callers avoid an explicit
/// `_has` + `_get` pair.
void *rt_frozenmap_get_or(void *obj, rt_string key, void *default_value) {
    if (!obj || !key)
        return default_value;
    fm_slot *s = fm_find(as_frozenmap(obj, "FrozenMap: invalid FrozenMap object"), key);
    return s ? s->value : default_value;
}

/// @brief Build a new frozen map that contains every entry from `obj` plus every entry from
/// `other`. Keys present in both maps take the value from `other` (override semantics). Traps
/// on count overflow.
void *rt_frozenmap_merge(void *obj, void *other) {
    int64_t la = rt_frozenmap_len(obj);
    int64_t lb = rt_frozenmap_len(other);
    if (la > INT64_MAX - lb)
        rt_trap("FrozenMap: merge size overflow");
    rt_frozenmap_impl *fm = fm_alloc(la + lb);

    // Insert from first map
    if (obj) {
        rt_frozenmap_impl *a = as_frozenmap(obj, "FrozenMap: invalid FrozenMap object");
        for (int64_t i = 0; i < a->capacity; i++) {
            if (a->slots[i].key)
                fm_insert(fm, a->slots[i].key, a->slots[i].value);
        }
    }
    // Insert from second map (overwrites on conflict)
    if (other) {
        rt_frozenmap_impl *b = as_frozenmap(other, "FrozenMap: invalid FrozenMap object");
        for (int64_t i = 0; i < b->capacity; i++) {
            if (b->slots[i].key)
                fm_insert(fm, b->slots[i].key, b->slots[i].value);
        }
    }
    return (void *)fm;
}

static int8_t fm_value_equals(void *a, void *b) {
    return a == b || rt_box_equal(a, b);
}

/// @brief Compare two frozen maps for structural equality.
/// @details Two frozen maps are equal when they contain the same key-value
///          pairs. Order does not matter since the comparison checks
///          membership in both directions.
/// @brief Returns 1 if both maps have identical key→value sets (order-independent comparison).
int8_t rt_frozenmap_equals(void *obj, void *other) {
    int64_t la = rt_frozenmap_len(obj);
    int64_t lb = rt_frozenmap_len(other);
    if (la != lb)
        return 0;

    if (!obj)
        return 1; // both empty
    if (!other)
        return 1; // both empty after matching counts

    rt_frozenmap_impl *a = as_frozenmap(obj, "FrozenMap: invalid FrozenMap object");
    rt_frozenmap_impl *b = as_frozenmap(other, "FrozenMap: invalid FrozenMap object");

    for (int64_t i = 0; i < a->capacity; i++) {
        if (a->slots[i].key) {
            fm_slot *bs = fm_find(b, a->slots[i].key);
            if (!bs || !fm_value_equals(bs->value, a->slots[i].value))
                return 0;
        }
    }
    return 1;
}
