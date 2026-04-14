//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_iter.c
// Purpose: Implements a unified stateful iterator that works across all Viper
//   collection types (Seq, List, Ring, Deque, Map, Set, Stack). Iterators wrap
//   a collection pointer and a current position index; Next() advances the
//   position and returns the element, HasNext() checks bounds.
//
// Key invariants:
//   - For indexed GC-managed collections (Seq, List, Ring), the iterator retains
//     a reference to the source collection and iterates by index directly.
//   - For unindexed or malloc-managed collections (Deque, Map, Set, Stack), the
//     iterator snapshots the collection into a Seq at creation time and iterates
//     the snapshot. This means mutations to the source after iterator creation
//     are NOT visible.
//   - The `len` field is cached at creation from the source length; it does not
//     update if the source is mutated after the iterator is created.
//   - Calling Next() when HasNext() returns 0 returns NULL and does not advance
//     past the end (pos stays at len).
//   - The iterator holds a retained reference to the source/snapshot Seq; the
//     finalizer (iter_finalizer) releases it when the iterator is collected.
//
// Ownership/Lifetime:
//   - Iterator objects are GC-managed (rt_obj_new_i64). The iter_finalizer
//     releases the source/snapshot reference when the iterator is collected.
//
// Links: src/runtime/collections/rt_iter.h (public API),
//        src/runtime/collections/rt_seq.h, rt_list.h, rt_ring.h, rt_deque.h,
//        rt_map.h, rt_set.h, rt_stack.h (iterable collections)
//
//===----------------------------------------------------------------------===//

#include "rt_iter.h"

#include "rt_deque.h"
#include "rt_internal.h"
#include "rt_list.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_ring.h"
#include "rt_seq.h"
#include "rt_set.h"
#include "rt_stack.h"

#include <stdlib.h>
#include <string.h>

/// Iterator source kind. All sources are heap-managed (rt_obj_new_i64).
typedef enum {
    ITER_SEQ,
    ITER_LIST,
    ITER_RING,
    ITER_SNAPSHOT ///< Backed by a captured Seq snapshot (for Deque, Map, Set, Stack)
} iter_kind;

/// Internal iterator state.
typedef struct {
    void *vptr;
    void *source; ///< Retained reference to the original collection or snapshot Seq
    iter_kind kind;
    int64_t pos; ///< Current position (next element to return)
    int64_t len; ///< Cached length at creation time
} rt_iter_impl;

static void iter_finalizer(void *obj) {
    rt_iter_impl *it = (rt_iter_impl *)obj;
    if (it && it->source) {
        if (rt_obj_release_check0(it->source))
            rt_obj_free(it->source);
        it->source = NULL;
    }
}

/// Create an iterator that retains source. Source MUST be a heap object.
static rt_iter_impl *make_iter(void *source, iter_kind kind, int64_t len) {
    rt_iter_impl *it;
    if (!source)
        return NULL;
    it = (rt_iter_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_iter_impl));
    if (!it) {
        rt_trap("Iterator: allocation failed");
        return NULL;
    }
    it->vptr = NULL;
    it->source = source;
    rt_obj_retain_maybe(source);
    it->kind = kind;
    it->pos = 0;
    it->len = len;
    rt_obj_set_finalizer(it, iter_finalizer);
    return it;
}

/// Create a snapshot iterator. Takes ownership of snapshot (does not add retain).
static rt_iter_impl *make_iter_snapshot(void *snapshot, int64_t len) {
    rt_iter_impl *it;
    if (!snapshot)
        return NULL;
    it = (rt_iter_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_iter_impl));
    if (!it) {
        /* Failed to create iterator — release the snapshot we own. */
        if (rt_obj_release_check0(snapshot))
            rt_obj_free(snapshot);
        rt_trap("Iterator: allocation failed");
        return NULL;
    }
    it->vptr = NULL;
    it->source = snapshot;
    /* No retain: we take ownership of the snapshot's creation reference. */
    it->kind = ITER_SNAPSHOT;
    it->pos = 0;
    it->len = len;
    rt_obj_set_finalizer(it, iter_finalizer);
    return it;
}

//=============================================================================
// Factory functions
//
// Each `_from_*` builds an iterator over an existing collection. SEQ, LIST,
// and RING support live (in-place) iteration — the iterator stores the
// collection by reference. DEQUE, MAP, SET, and STACK are *snapshotted* into
// a fresh Seq because their underlying storage either isn't GC-managed or
// doesn't support indexed access; the snapshot freezes the state at iter
// creation time.
//=============================================================================

/// @brief Build a live iterator over a Seq. Subsequent mutations of the seq are visible.
void *rt_iter_from_seq(void *seq) {
    if (!seq)
        return NULL;
    return make_iter(seq, ITER_SEQ, rt_seq_len(seq));
}

/// @brief Build a live iterator over a List. Subsequent mutations are visible.
void *rt_iter_from_list(void *list) {
    if (!list)
        return NULL;
    return make_iter(list, ITER_LIST, rt_list_len(list));
}

/// @brief Snapshot a Deque into an iterator. Deques use plain malloc (not GC), so we copy
/// every element into a fresh Seq at iter creation. Later mutations of the deque are NOT seen.
void *rt_iter_from_deque(void *deque) {
    void *snapshot;
    int64_t len, i;
    if (!deque)
        return NULL;
    /* Deque uses plain malloc, not rt_obj_new_i64, so we cannot retain it.
       Snapshot all elements into a heap-managed Seq instead. */
    len = rt_deque_len(deque);
    snapshot = rt_seq_new();
    if (!snapshot)
        return NULL;
    for (i = 0; i < len; i++)
        rt_seq_push(snapshot, rt_deque_get(deque, i));
    return make_iter_snapshot(snapshot, len);
}

/// @brief Build a live iterator over a ring buffer. Subsequent mutations are visible.
void *rt_iter_from_ring(void *ring) {
    if (!ring)
        return NULL;
    return make_iter(ring, ITER_RING, rt_ring_len(ring));
}

/// @brief Snapshot the keys of a Map into an iterator (insertion order).
void *rt_iter_from_map_keys(void *map) {
    void *keys;
    if (!map)
        return NULL;
    keys = rt_map_keys(map);
    if (!keys)
        return NULL;
    return make_iter_snapshot(keys, rt_seq_len(keys));
}

/// @brief Snapshot the values of a Map into an iterator (insertion order).
void *rt_iter_from_map_values(void *map) {
    void *values;
    if (!map)
        return NULL;
    values = rt_map_values(map);
    if (!values)
        return NULL;
    return make_iter_snapshot(values, rt_seq_len(values));
}

/// @brief Snapshot a Set into an iterator. Order is the set's internal hashing order — not
/// guaranteed stable across versions. Mutations of the source set after iter creation are not seen.
void *rt_iter_from_set(void *set) {
    void *items;
    if (!set)
        return NULL;
    items = rt_set_items(set);
    if (!items)
        return NULL;
    return make_iter_snapshot(items, rt_seq_len(items));
}

/// @brief Currently returns an empty-snapshot iterator — Stack has no indexed access. Convert
/// the stack to a Seq first if you need real iteration.
void *rt_iter_from_stack(void *stack) {
    void *snapshot;
    if (!stack)
        return NULL;
    /* Stack has no indexed access, so we produce an empty snapshot.
       Users should convert stack to seq first for full iteration. */
    snapshot = rt_seq_new();
    if (!snapshot)
        return NULL;
    return make_iter_snapshot(snapshot, 0);
}

//=============================================================================
// Core iteration
//=============================================================================

static void *get_element(rt_iter_impl *it, int64_t idx) {
    switch (it->kind) {
        case ITER_SEQ:
        case ITER_SNAPSHOT:
            return rt_seq_get(it->source, idx);
        case ITER_LIST:
            return rt_list_get(it->source, idx);
        case ITER_RING:
            return rt_ring_get(it->source, idx);
    }
    return NULL;
}

/// @brief Check whether the iterator has more elements.
/// @details Returns true if the current position is before the end.
int8_t rt_iter_has_next(void *iter) {
    rt_iter_impl *it;
    if (!iter)
        return 0;
    it = (rt_iter_impl *)iter;
    return (it->pos < it->len) ? 1 : 0;
}

/// @brief Return the current element and advance the cursor. Returns NULL when exhausted.
/// Pair with `_has_next` to drive while-loop iteration.
void *rt_iter_next(void *iter) {
    rt_iter_impl *it;
    void *elem;
    if (!iter)
        return NULL;
    it = (rt_iter_impl *)iter;
    if (it->pos >= it->len)
        return NULL;
    elem = get_element(it, it->pos);
    it->pos++;
    return elem;
}

/// @brief Look at the current element without advancing the cursor.
void *rt_iter_peek(void *iter) {
    rt_iter_impl *it;
    if (!iter)
        return NULL;
    it = (rt_iter_impl *)iter;
    if (it->pos >= it->len)
        return NULL;
    return get_element(it, it->pos);
}

/// @brief Reset the iterator to the beginning of the collection.
void rt_iter_reset(void *iter) {
    if (!iter)
        return;
    ((rt_iter_impl *)iter)->pos = 0;
}

/// @brief Return the current position (0-based index) of the iterator.
/// @param iter Iterator object pointer; returns 0 if NULL.
/// @return Current index within the underlying collection.
int64_t rt_iter_index(void *iter) {
    if (!iter)
        return 0;
    return ((rt_iter_impl *)iter)->pos;
}

/// @brief Return the total number of elements in the iterable collection.
int64_t rt_iter_count(void *iter) {
    if (!iter)
        return 0;
    return ((rt_iter_impl *)iter)->len;
}

/// @brief Drain the remaining iterator elements into a fresh Seq. Advances the cursor to end.
void *rt_iter_to_seq(void *iter) {
    rt_iter_impl *it;
    void *seq;
    if (!iter)
        return rt_seq_new();
    it = (rt_iter_impl *)iter;
    seq = rt_seq_new();
    while (it->pos < it->len) {
        void *elem = get_element(it, it->pos);
        rt_seq_push(seq, elem);
        it->pos++;
    }
    return seq;
}

/// @brief Advance the iterator by up to @p n positions, returning the count skipped.
/// @details Moves the cursor forward without returning the intermediate elements.
///          Stops at the end of the collection if fewer than @p n elements remain.
/// @param iter Iterator object pointer; returns 0 if NULL.
/// @param n Maximum number of elements to skip (must be positive).
/// @return Number of elements actually skipped.
int64_t rt_iter_skip(void *iter, int64_t n) {
    rt_iter_impl *it;
    int64_t remaining, skipped;
    if (!iter || n <= 0)
        return 0;
    it = (rt_iter_impl *)iter;
    remaining = it->len - it->pos;
    skipped = (n < remaining) ? n : remaining;
    it->pos += skipped;
    return skipped;
}
