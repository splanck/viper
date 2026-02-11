//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_iter.c
/// @brief Unified stateful iterator for all collections.
///
/// Iterators wrap a collection pointer + position index. For heap-managed
/// indexed collections (Seq, List, Ring) we iterate directly by retaining
/// the source. For malloc-based collections (Deque) or unindexed collections
/// (Map, Set, Stack) we snapshot to a Seq on creation and iterate that.
///
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
typedef enum
{
    ITER_SEQ,
    ITER_LIST,
    ITER_RING,
    ITER_SNAPSHOT ///< Backed by a captured Seq snapshot (for Deque, Map, Set, Stack)
} iter_kind;

/// Internal iterator state.
typedef struct
{
    void *vptr;
    void *source; ///< Retained reference to the original collection or snapshot Seq
    iter_kind kind;
    int64_t pos; ///< Current position (next element to return)
    int64_t len; ///< Cached length at creation time
} rt_iter_impl;

static void iter_finalizer(void *obj)
{
    rt_iter_impl *it = (rt_iter_impl *)obj;
    if (it && it->source)
    {
        if (rt_obj_release_check0(it->source))
            rt_obj_free(it->source);
        it->source = NULL;
    }
}

/// Create an iterator that retains source. Source MUST be a heap object.
static rt_iter_impl *make_iter(void *source, iter_kind kind, int64_t len)
{
    rt_iter_impl *it;
    if (!source)
        return NULL;
    it = (rt_iter_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_iter_impl));
    if (!it)
    {
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
static rt_iter_impl *make_iter_snapshot(void *snapshot, int64_t len)
{
    rt_iter_impl *it;
    if (!snapshot)
        return NULL;
    it = (rt_iter_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_iter_impl));
    if (!it)
    {
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
//=============================================================================

void *rt_iter_from_seq(void *seq)
{
    if (!seq)
        return NULL;
    return make_iter(seq, ITER_SEQ, rt_seq_len(seq));
}

void *rt_iter_from_list(void *list)
{
    if (!list)
        return NULL;
    return make_iter(list, ITER_LIST, rt_list_len(list));
}

void *rt_iter_from_deque(void *deque)
{
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

void *rt_iter_from_ring(void *ring)
{
    if (!ring)
        return NULL;
    return make_iter(ring, ITER_RING, rt_ring_len(ring));
}

void *rt_iter_from_map_keys(void *map)
{
    void *keys;
    if (!map)
        return NULL;
    keys = rt_map_keys(map);
    if (!keys)
        return NULL;
    return make_iter_snapshot(keys, rt_seq_len(keys));
}

void *rt_iter_from_map_values(void *map)
{
    void *values;
    if (!map)
        return NULL;
    values = rt_map_values(map);
    if (!values)
        return NULL;
    return make_iter_snapshot(values, rt_seq_len(values));
}

void *rt_iter_from_set(void *set)
{
    void *items;
    if (!set)
        return NULL;
    items = rt_set_items(set);
    if (!items)
        return NULL;
    return make_iter_snapshot(items, rt_seq_len(items));
}

void *rt_iter_from_stack(void *stack)
{
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

static void *get_element(rt_iter_impl *it, int64_t idx)
{
    switch (it->kind)
    {
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

int8_t rt_iter_has_next(void *iter)
{
    rt_iter_impl *it;
    if (!iter)
        return 0;
    it = (rt_iter_impl *)iter;
    return (it->pos < it->len) ? 1 : 0;
}

void *rt_iter_next(void *iter)
{
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

void *rt_iter_peek(void *iter)
{
    rt_iter_impl *it;
    if (!iter)
        return NULL;
    it = (rt_iter_impl *)iter;
    if (it->pos >= it->len)
        return NULL;
    return get_element(it, it->pos);
}

void rt_iter_reset(void *iter)
{
    if (!iter)
        return;
    ((rt_iter_impl *)iter)->pos = 0;
}

int64_t rt_iter_index(void *iter)
{
    if (!iter)
        return 0;
    return ((rt_iter_impl *)iter)->pos;
}

int64_t rt_iter_count(void *iter)
{
    if (!iter)
        return 0;
    return ((rt_iter_impl *)iter)->len;
}

void *rt_iter_to_seq(void *iter)
{
    rt_iter_impl *it;
    void *seq;
    if (!iter)
        return rt_seq_new();
    it = (rt_iter_impl *)iter;
    seq = rt_seq_new();
    while (it->pos < it->len)
    {
        void *elem = get_element(it, it->pos);
        rt_seq_push(seq, elem);
        it->pos++;
    }
    return seq;
}

int64_t rt_iter_skip(void *iter, int64_t n)
{
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
