//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_frozenset.c
// Purpose: Implements an immutable string set (FrozenSet) built once from a Seq
//   of strings. After construction no elements can be added or removed. Uses
//   open-addressing with FNV-1a hashing for O(1) average-case membership tests.
//   Typical uses: constant keyword tables, stop-word lists, and any membership
//   query where the set contents are known at build time.
//
// Key invariants:
//   - Open-addressing hash table; slot array is sized to 2× the element count
//     at construction so load factor stays below 50%.
//   - Slot key == NULL indicates an empty slot; no tombstones needed since the
//     set is immutable after build.
//   - FNV-1a hash over the raw string bytes; linear probing on collision.
//   - Keys are stored as retained rt_string references (not copied); the
//     FrozenSet keeps references to prevent GC collection of key strings.
//   - Contains returns 1 if the string is present, 0 otherwise.
//   - Safe for concurrent read-only access after construction completes.
//
// Ownership/Lifetime:
//   - FrozenSet objects are GC-managed (rt_obj_new_i64). The slots array is
//     freed by the GC finalizer (frozenset_finalizer).
//
// Links: src/runtime/collections/rt_frozenset.h (public API),
//        src/runtime/collections/rt_set.h (mutable set counterpart)
//
//===----------------------------------------------------------------------===//

#include "rt_frozenset.h"

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

// --- Helper: extract string from seq element (may be boxed) ---

static rt_string fs_extract_str(void *elem) {
    if (!elem)
        return NULL;
    if (rt_string_is_handle(elem))
        return (rt_string)elem;
    // Not a raw string -- assume boxed value and unbox.
    return rt_unbox_str(elem);
}

// --- Hash table entry (open addressing) ---

typedef struct {
    rt_string key; // NULL = empty slot
} fs_slot;

typedef struct {
    void *vptr;
    int64_t count;
    int64_t capacity;
    fs_slot *slots;
} rt_frozenset_impl;

// --- FNV-1a hash ---

static uint64_t fs_hash(const char *data, int64_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (int64_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t fs_str_hash(rt_string s) {
    const char *cstr = rt_string_cstr(s);
    return fs_hash(cstr, rt_str_len(s));
}

// --- Internal helpers ---

static void fs_finalizer(void *obj) {
    rt_frozenset_impl *fs = (rt_frozenset_impl *)obj;
    if (fs->slots) {
        for (int64_t i = 0; i < fs->capacity; i++) {
            if (fs->slots[i].key)
                rt_string_unref(fs->slots[i].key);
        }
        free(fs->slots);
        fs->slots = NULL;
    }
}

static int64_t fs_next_pow2(int64_t n) {
    int64_t p = 16;
    while (p < n) {
        if (p > INT64_MAX / 2)
            rt_trap("FrozenSet: capacity overflow");
        p *= 2;
    }
    return p;
}

static rt_frozenset_impl *fs_alloc(int64_t count) {
    // Use ~50% load factor for good probe performance
    int64_t cap = fs_next_pow2(count < 4 ? 8 : count * 2);
    rt_frozenset_impl *fs = (rt_frozenset_impl *)rt_obj_new_i64(0, sizeof(rt_frozenset_impl));
    fs->count = 0;
    fs->capacity = cap;
    fs->slots = (fs_slot *)calloc((size_t)cap, sizeof(fs_slot));
    if (!fs->slots)
        rt_trap("rt_frozenset: memory allocation failed");
    rt_obj_set_finalizer(fs, fs_finalizer);
    return fs;
}

static int8_t fs_insert(rt_frozenset_impl *fs, rt_string key) {
    uint64_t h = fs_str_hash(key);
    int64_t mask = fs->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);
    const char *key_cstr = rt_string_cstr(key);

    for (int64_t i = 0; i < fs->capacity; i++) {
        int64_t slot = (idx + i) & mask;
        if (!fs->slots[slot].key) {
            fs->slots[slot].key = key;
            rt_obj_retain_maybe(key);
            fs->count++;
            return 1;
        }
        if (strcmp(rt_string_cstr(fs->slots[slot].key), key_cstr) == 0)
            return 0; // duplicate
    }
    return 0;
}

static int8_t fs_contains(rt_frozenset_impl *fs, rt_string key) {
    if (!fs || fs->count == 0)
        return 0;
    uint64_t h = fs_str_hash(key);
    int64_t mask = fs->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);
    const char *key_cstr = rt_string_cstr(key);

    for (int64_t i = 0; i < fs->capacity; i++) {
        int64_t slot = (idx + i) & mask;
        if (!fs->slots[slot].key)
            return 0;
        if (strcmp(rt_string_cstr(fs->slots[slot].key), key_cstr) == 0)
            return 1;
    }
    return 0;
}

// --- Public API ---

/// @brief Build an immutable set from a Seq of strings (duplicates collapse). Internal storage
/// is an open-addressed hash table sized for the entry count. Use mutable Set elsewhere.
void *rt_frozenset_from_seq(void *items) {
    if (!items)
        return (void *)fs_alloc(0);

    int64_t n = rt_seq_len(items);
    rt_frozenset_impl *fs = fs_alloc(n);

    for (int64_t i = 0; i < n; i++) {
        rt_string elem = fs_extract_str(rt_seq_get(items, i));
        if (elem)
            fs_insert(fs, elem);
    }
    return (void *)fs;
}

/// @brief Construct an empty frozen set.
void *rt_frozenset_empty(void) {
    return (void *)fs_alloc(0);
}

/// @brief Return the number of elements in the frozen (immutable) set.
int64_t rt_frozenset_len(void *obj) {
    if (!obj)
        return 0;
    return ((rt_frozenset_impl *)obj)->count;
}

/// @brief Check whether the frozen set has no elements.
int8_t rt_frozenset_is_empty(void *obj) {
    return rt_frozenset_len(obj) == 0 ? 1 : 0;
}

/// @brief Check whether an element exists in the frozen set.
/// @details Uses hash-based lookup on the immutable backing array.
int8_t rt_frozenset_has(void *obj, rt_string elem) {
    if (!obj || !elem)
        return 0;
    return fs_contains((rt_frozenset_impl *)obj, elem);
}

/// @brief Return a Seq of every element in the set (slot-iteration order, not insertion order).
void *rt_frozenset_items(void *obj) {
    void *seq = rt_seq_new();
    if (!obj)
        return seq;

    rt_frozenset_impl *fs = (rt_frozenset_impl *)obj;
    for (int64_t i = 0; i < fs->capacity; i++) {
        if (fs->slots[i].key)
            rt_seq_push(seq, fs->slots[i].key);
    }
    return seq;
}

/// @brief Return a fresh frozen set containing every element from either operand. Duplicates
/// (elements present in both) appear once. Implemented by zipping into a Seq then re-freezing.
void *rt_frozenset_union(void *obj, void *other) {
    // Collect all elements from both sets
    void *seq = rt_seq_new();

    if (obj) {
        rt_frozenset_impl *a = (rt_frozenset_impl *)obj;
        for (int64_t i = 0; i < a->capacity; i++) {
            if (a->slots[i].key)
                rt_seq_push(seq, a->slots[i].key);
        }
    }
    if (other) {
        rt_frozenset_impl *b = (rt_frozenset_impl *)other;
        for (int64_t i = 0; i < b->capacity; i++) {
            if (b->slots[i].key)
                rt_seq_push(seq, b->slots[i].key);
        }
    }

    void *result = rt_frozenset_from_seq(seq);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);
    return result;
}

/// @brief Return a fresh frozen set containing only elements present in both operands.
/// Returns an empty set if either operand is NULL.
void *rt_frozenset_intersect(void *obj, void *other) {
    void *seq = rt_seq_new();
    if (!obj || !other) {
        void *result = rt_frozenset_from_seq(seq);
        if (rt_obj_release_check0(seq))
            rt_obj_free(seq);
        return result;
    }

    rt_frozenset_impl *a = (rt_frozenset_impl *)obj;
    rt_frozenset_impl *b = (rt_frozenset_impl *)other;

    for (int64_t i = 0; i < a->capacity; i++) {
        if (a->slots[i].key && fs_contains(b, a->slots[i].key))
            rt_seq_push(seq, a->slots[i].key);
    }

    void *result = rt_frozenset_from_seq(seq);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);
    return result;
}

/// @brief Return a fresh frozen set containing elements present in `obj` but not in `other`.
/// `obj - other` set semantics. NULL `other` returns a copy of `obj`.
void *rt_frozenset_diff(void *obj, void *other) {
    void *seq = rt_seq_new();
    if (!obj) {
        void *result = rt_frozenset_from_seq(seq);
        if (rt_obj_release_check0(seq))
            rt_obj_free(seq);
        return result;
    }

    rt_frozenset_impl *a = (rt_frozenset_impl *)obj;

    for (int64_t i = 0; i < a->capacity; i++) {
        if (a->slots[i].key) {
            if (!other || !fs_contains((rt_frozenset_impl *)other, a->slots[i].key))
                rt_seq_push(seq, a->slots[i].key);
        }
    }

    void *result = rt_frozenset_from_seq(seq);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);
    return result;
}

/// @brief Check whether this frozen set is a subset of another.
/// @details Every element in this set must also be present in the other.
int8_t rt_frozenset_is_subset(void *obj, void *other) {
    if (!obj)
        return 1; // empty set is subset of everything
    if (!other)
        return ((rt_frozenset_impl *)obj)->count == 0 ? 1 : 0;

    rt_frozenset_impl *a = (rt_frozenset_impl *)obj;
    rt_frozenset_impl *b = (rt_frozenset_impl *)other;

    for (int64_t i = 0; i < a->capacity; i++) {
        if (a->slots[i].key && !fs_contains(b, a->slots[i].key))
            return 0;
    }
    return 1;
}

/// @brief Compare two frozen sets for structural equality.
/// @details Two frozen sets are equal when they contain the same elements.
int8_t rt_frozenset_equals(void *obj, void *other) {
    int64_t la = rt_frozenset_len(obj);
    int64_t lb = rt_frozenset_len(other);
    if (la != lb)
        return 0;
    return rt_frozenset_is_subset(obj, other);
}
