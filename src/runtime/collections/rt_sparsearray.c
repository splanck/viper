//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_sparsearray.c
// Purpose: Implements a sparse integer-indexed array using open-addressing
//   hash map semantics (int64_t key -> void* value). Only indices that have
//   been explicitly set occupy memory; unset indices return NULL. Suitable for
//   large, sparsely populated index spaces where most indices are absent.
//
// Key invariants:
//   - Open-addressing hash table with 64-bit mix hash on the integer key to
//     avoid clustering from sequential indices.
//   - Initial capacity is configurable; probing is linear (index + i) % cap.
//   - Load factor kept below 2/3 via resize. Capacity is always a power of two
//     (required for bitmask indexing: slot = hash & (capacity - 1)).
//   - `occupied` flag distinguishes empty slots from slots holding a NULL value.
//   - Get returns NULL for missing indices; no error is raised.
//   - Set with NULL value removes the entry (slot becomes unoccupied).
//   - Tombstones are not used; the entire table is rehashed on remove during
//     resize, or a swap-with-probe approach is used to maintain probe chains.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - SparseArray objects are GC-managed (rt_obj_new_i64). The slots array is
//     freed by the GC finalizer (sa_finalizer), which also releases retained
//     value references via rt_obj_release_check0.
//
// Links: src/runtime/collections/rt_sparsearray.h (public API),
//        src/runtime/collections/rt_intmap.h (similar integer-keyed map)
//
//===----------------------------------------------------------------------===//

#include "rt_sparsearray.h"

#include "rt_box.h"
#include "rt_collection_ids.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stdlib.h>
#include <string.h>

// --- Open addressing hash map: int64_t -> void* ---

typedef struct {
    int64_t key;
    void *value;
    int8_t occupied;
} sa_slot;

typedef struct {
    void *vptr;
    int64_t count;
    int64_t capacity;
    sa_slot *slots;
} rt_sparse_impl;

/// @brief Checked cast of an opaque handle to the SparseArray implementation;
///        traps with @p what if @p obj is NULL or not a SparseArray.
static rt_sparse_impl *as_sparse(void *obj, const char *what) {
    if (!rt_obj_is_instance(obj, RT_SPARSEARRAY_CLASS_ID, sizeof(rt_sparse_impl))) {
        rt_trap(what);
        return NULL;
    }
    return (rt_sparse_impl *)obj;
}

// --- Hash function for int64 ---

/// @brief 64-bit integer mix hash (Murmur3 fmix64) for sparse-array keys.
static uint64_t sa_hash(int64_t key) {
    uint64_t k = (uint64_t)key;
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// --- Internal helpers ---

/// @brief Drop one GC reference to a stored value and free it at zero.
static void sa_release_value(void *value) {
    if (value && rt_obj_release_check0(value))
        rt_obj_free(value);
}

/// @brief GC finalizer: release every occupied slot's value, free the slots.
static void sa_finalizer(void *obj) {
    if (!obj)
        return;
    rt_sparse_impl *sa = as_sparse(obj, "SparseArray: invalid SparseArray object");
    if (sa->slots) {
        for (int64_t i = 0; i < sa->capacity; i++) {
            if (sa->slots[i].occupied)
                sa_release_value(sa->slots[i].value);
        }
        free(sa->slots);
        sa->slots = NULL;
    }
    sa->capacity = 0;
    sa->count = 0;
}

/// @brief GC traversal: visit the value of every occupied slot.
static void sa_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    if (!obj || !visitor)
        return;
    rt_sparse_impl *sa = as_sparse(obj, "SparseArray: invalid SparseArray object");
    if (!sa->slots)
        return;
    for (int64_t i = 0; i < sa->capacity; i++) {
        if (sa->slots[i].occupied)
            visitor(sa->slots[i].value, ctx);
    }
}

static int sa_grow(rt_sparse_impl *sa);

/// @brief Open-addressed (linear-probe) insert/update of @p key→@p value;
///        retains the value, releasing a replaced one. Assumes spare capacity.
static void sa_insert_internal(rt_sparse_impl *sa, int64_t key, void *value) {
    uint64_t h = sa_hash(key);
    int64_t mask = sa->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);

    for (int64_t i = 0; i < sa->capacity; i++) {
        int64_t slot = (idx + i) & mask;
        if (!sa->slots[slot].occupied) {
            rt_obj_retain_maybe(value);
            sa->slots[slot].key = key;
            sa->slots[slot].value = value;
            sa->slots[slot].occupied = 1;
            sa->count++;
            return;
        }
        if (sa->slots[slot].key == key) {
            // Update value
            rt_obj_retain_maybe(value);
            sa_release_value(sa->slots[slot].value);
            sa->slots[slot].value = value;
            return;
        }
    }
}

/// @brief Double the slot table and re-insert occupied entries (ownership
///        transferred, no extra retain). Traps on overflow/OOM.
static int sa_grow(rt_sparse_impl *sa) {
    int64_t old_cap = sa->capacity;
    sa_slot *old_slots = sa->slots;

    if (old_cap > INT64_MAX / 2) {
        rt_trap("SparseArray: capacity overflow");
        return 0;
    }
    int64_t new_cap = old_cap * 2;
    if ((uint64_t)new_cap > SIZE_MAX / sizeof(sa_slot)) {
        rt_trap("SparseArray: allocation size overflow");
        return 0;
    }
    sa_slot *new_slots = (sa_slot *)calloc((size_t)new_cap, sizeof(sa_slot));
    if (!new_slots) {
        rt_trap("SparseArray: memory allocation failed");
        return 0;
    }
    sa->capacity = new_cap;
    sa->slots = new_slots;
    sa->count = 0;

    for (int64_t i = 0; i < old_cap; i++) {
        if (old_slots[i].occupied) {
            // Re-insert without retain (we already hold a ref)
            uint64_t h = sa_hash(old_slots[i].key);
            int64_t mask = sa->capacity - 1;
            int64_t idx = (int64_t)(h & (uint64_t)mask);

            for (int64_t j = 0; j < sa->capacity; j++) {
                int64_t slot = (idx + j) & mask;
                if (!sa->slots[slot].occupied) {
                    sa->slots[slot].key = old_slots[i].key;
                    sa->slots[slot].value = old_slots[i].value;
                    sa->slots[slot].occupied = 1;
                    sa->count++;
                    break;
                }
            }
        }
    }
    free(old_slots);
    return 1;
}

/// @brief Linear-probe lookup of @p key; returns its slot or NULL if absent.
static sa_slot *sa_find(rt_sparse_impl *sa, int64_t key) {
    if (!sa || sa->count == 0)
        return NULL;
    uint64_t h = sa_hash(key);
    int64_t mask = sa->capacity - 1;
    int64_t idx = (int64_t)(h & (uint64_t)mask);

    for (int64_t i = 0; i < sa->capacity; i++) {
        int64_t slot = (idx + i) & mask;
        if (!sa->slots[slot].occupied)
            return NULL;
        if (sa->slots[slot].key == key)
            return &sa->slots[slot];
    }
    return NULL;
}

// --- Public API ---

/// @brief Construct a sparse array (int64 → value). Open-addressed hash with linear probing,
/// capacity grows past 70% load. Suitable when most indices in a logical range are unused
/// (e.g., entity IDs scattered across a wide ID space).
void *rt_sparse_new(void) {
    rt_sparse_impl *sa =
        (rt_sparse_impl *)rt_obj_new_i64(RT_SPARSEARRAY_CLASS_ID, sizeof(rt_sparse_impl));
    if (!sa) {
        rt_trap("SparseArray: memory allocation failed");
        return NULL;
    }
    sa->count = 0;
    sa->capacity = 16;
    sa->slots = (sa_slot *)calloc(16, sizeof(sa_slot));
    if (!sa->slots) {
        if (rt_obj_release_check0(sa))
            rt_obj_free(sa);
        rt_trap("SparseArray: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(sa, sa_finalizer);
    rt_gc_track(sa, sa_traverse);
    return (void *)sa;
}

/// @brief Return the number of populated entries in the sparse array.
/// @param obj Sparse array object pointer; returns 0 if NULL.
/// @return Count of occupied slots.
int64_t rt_sparse_len(void *obj) {
    if (!obj)
        return 0;
    return as_sparse(obj, "SparseArray.Len: invalid SparseArray object")->count;
}

/// @brief Read the value stored at `index`, or NULL if absent.
void *rt_sparse_get(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    sa_slot *s = sa_find(as_sparse(obj, "SparseArray.Get: invalid SparseArray object"), index);
    return s ? s->value : NULL;
}

/// @brief Set the value at a sparse index, growing the table if needed.
/// @details Uses open addressing with linear probing. Grows the table when
///          load factor exceeds 70% to maintain O(1) amortized access.
/// @param obj Sparse array object pointer; no-op if NULL.
/// @param index Sparse key to store at.
/// @param value Value to associate with the index.
void rt_sparse_set(void *obj, int64_t index, void *value) {
    if (!obj)
        return;
    rt_sparse_impl *sa = as_sparse(obj, "SparseArray.Set: invalid SparseArray object");
    if (!sa)
        return;

    if (!value) {
        rt_sparse_remove(obj, index);
        return;
    }

    // Check load factor (> 70%)
    if ((long double)sa->count * 10.0L >= (long double)sa->capacity * 7.0L && !sa_grow(sa))
        return;

    sa_insert_internal(sa, index, value);
}

/// @brief Check whether a value exists at the given sparse index.
/// @param obj Sparse array object pointer; returns 0 if NULL.
/// @param index Sparse key to look up.
/// @return 1 if the index is occupied, 0 otherwise.
int8_t rt_sparse_has(void *obj, int64_t index) {
    if (!obj)
        return 0;
    return sa_find(as_sparse(obj, "SparseArray.Has: invalid SparseArray object"), index) != NULL
               ? 1
               : 0;
}

/// @brief Remove the value at a sparse index, rehashing displaced neighbors.
/// @details Uses backward-shift deletion: after clearing the slot, any
///          subsequent occupied slots that were displaced by probing are
///          re-inserted to maintain correct lookup behavior.
/// @param obj Sparse array object pointer; returns 0 if NULL.
/// @param index Sparse key to remove.
/// @return 1 if the entry was found and removed, 0 otherwise.
int8_t rt_sparse_remove(void *obj, int64_t index) {
    if (!obj)
        return 0;
    rt_sparse_impl *sa = as_sparse(obj, "SparseArray.Remove: invalid SparseArray object");
    sa_slot *s = sa_find(sa, index);
    if (!s)
        return 0;

    sa_release_value(s->value);
    s->value = NULL;
    s->occupied = 0;
    sa->count--;

    // Rehash subsequent entries that may have been displaced
    int64_t mask = sa->capacity - 1;
    int64_t pos = (int64_t)(s - sa->slots);
    int64_t next = (pos + 1) & mask;

    while (sa->slots[next].occupied) {
        sa_slot tmp = sa->slots[next];
        sa->slots[next].occupied = 0;
        sa->count--;
        // Re-insert without ref counting (already held)
        uint64_t h = sa_hash(tmp.key);
        int64_t idx = (int64_t)(h & (uint64_t)mask);
        for (int64_t i = 0; i < sa->capacity; i++) {
            int64_t slot = (idx + i) & mask;
            if (!sa->slots[slot].occupied) {
                sa->slots[slot] = tmp;
                sa->count++;
                break;
            }
        }
        next = (next + 1) & mask;
    }

    return 1;
}

/// @brief Return a Seq of every populated index as boxed i64 values.
/// Slot-iteration order, not insertion order. Snapshot at call time.
void *rt_sparse_indices(void *obj) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!obj)
        return seq;
    rt_sparse_impl *sa = as_sparse(obj, "SparseArray.Indices: invalid SparseArray object");
    for (int64_t i = 0; i < sa->capacity; i++) {
        if (sa->slots[i].occupied) {
            void *boxed = rt_box_i64(sa->slots[i].key);
            rt_seq_push(seq, boxed);
            sa_release_value(boxed);
        }
    }
    return seq;
}

/// @brief Return a Seq of every stored value (parallel to `_indices` order).
void *rt_sparse_values(void *obj) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!obj)
        return seq;
    rt_sparse_impl *sa = as_sparse(obj, "SparseArray.Values: invalid SparseArray object");
    for (int64_t i = 0; i < sa->capacity; i++) {
        if (sa->slots[i].occupied)
            rt_seq_push(seq, sa->slots[i].value);
    }
    return seq;
}

/// @brief Remove all entries from the sparse array.
/// @details Releases all value references and marks all slots as unoccupied.
/// @param obj Sparse array object pointer; no-op if NULL.
void rt_sparse_clear(void *obj) {
    if (!obj)
        return;
    rt_sparse_impl *sa = as_sparse(obj, "SparseArray.Clear: invalid SparseArray object");
    for (int64_t i = 0; i < sa->capacity; i++) {
        if (sa->slots[i].occupied) {
            sa_release_value(sa->slots[i].value);
            sa->slots[i].occupied = 0;
            sa->slots[i].value = NULL;
        }
    }
    sa->count = 0;
}
