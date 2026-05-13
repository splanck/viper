//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCollectionsCorrectnessTests.cpp
// Purpose: Regression coverage for cross-cutting collection correctness fixes.
//
//===----------------------------------------------------------------------===//

#include "rt_bag.h"
#include "rt_bimap.h"
#include "rt_bitset.h"
#include "rt_bloomfilter.h"
#include "rt_collection_ids.h"
#include "rt_countmap.h"
#include "rt_defaultmap.h"
#include "rt_deque.h"
#include "rt_frozenmap.h"
#include "rt_frozenset.h"
#include "rt_intmap.h"
#include "rt_internal.h"
#include "rt_iter.h"
#include "rt_list.h"
#include "rt_lrucache.h"
#include "rt_map.h"
#include "rt_multimap.h"
#include "rt_object.h"
#include "rt_orderedmap.h"
#include "rt_pqueue.h"
#include "rt_queue.h"
#include "rt_ring.h"
#include "rt_seq.h"
#include "rt_set.h"
#include "rt_sortedset.h"
#include "rt_sparsearray.h"
#include "rt_stack.h"
#include "rt_string.h"
#include "rt_treemap.h"
#include "rt_trie.h"
#include "rt_unionfind.h"
#include "rt_weakmap.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}

namespace {

static int g_finalizer_calls = 0;

static void count_finalizer(void *) {
    ++g_finalizer_calls;
}

static void *new_obj() {
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static void release_obj(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void assert_class_id(void *obj, int64_t expected) {
    assert(obj != nullptr);
    assert(rt_obj_class_id(obj) == expected);
    release_obj(obj);
}

static void test_collection_class_ids_are_specific() {
    assert_class_id(rt_list_new(), RT_LIST_CLASS_ID);
    assert_class_id(rt_set_new(), RT_SET_CLASS_ID);
    assert_class_id(rt_stack_new(), RT_STACK_CLASS_ID);
    assert_class_id(rt_queue_new(), RT_QUEUE_CLASS_ID);
    assert_class_id(rt_ring_new(4), RT_RING_CLASS_ID);
    assert_class_id(rt_deque_new(), RT_DEQUE_CLASS_ID);
    assert_class_id(rt_bag_new(), RT_BAG_CLASS_ID);
    assert_class_id(rt_orderedmap_new(), RT_ORDEREDMAP_CLASS_ID);
    assert_class_id(rt_treemap_new(), RT_TREEMAP_CLASS_ID);
    assert_class_id(rt_frozenmap_empty(), RT_FROZENMAP_CLASS_ID);
    assert_class_id(rt_frozenset_empty(), RT_FROZENSET_CLASS_ID);
    assert_class_id(rt_sparse_new(), RT_SPARSEARRAY_CLASS_ID);
    assert_class_id(rt_intmap_new(), RT_INTMAP_CLASS_ID);
    assert_class_id(rt_defaultmap_new(nullptr), RT_DEFAULTMAP_CLASS_ID);
    assert_class_id(rt_multimap_new(), RT_MULTIMAP_CLASS_ID);
    assert_class_id(rt_lrucache_new(4), RT_LRUCACHE_CLASS_ID);
    assert_class_id(rt_trie_new(), RT_TRIE_CLASS_ID);
    assert_class_id(rt_bimap_new(), RT_BIMAP_CLASS_ID);
    assert_class_id(rt_bitset_new(8), RT_BITSET_CLASS_ID);
    assert_class_id(rt_bloomfilter_new(16, 0.01), RT_BLOOMFILTER_CLASS_ID);
    assert_class_id(rt_countmap_new(), RT_COUNTMAP_CLASS_ID);
    assert_class_id(rt_pqueue_new(), RT_PQUEUE_CLASS_ID);
    assert_class_id(rt_sortedset_new(), RT_SORTEDSET_CLASS_ID);
    assert_class_id(rt_unionfind_new(4), RT_UNIONFIND_CLASS_ID);
    assert_class_id(rt_weakmap_new(), RT_WEAKMAP_CLASS_ID);

    void *seq = rt_seq_new();
    void *iter = rt_iter_from_seq(seq);
    assert_class_id(iter, RT_ITERATOR_CLASS_ID);
    release_obj(seq);
}

static void test_map_values_snapshot_retains_values() {
    void *map = rt_map_new();
    void *value = new_obj();
    rt_string key = make_str("key");

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_map_set(map, key, value);
    void *values = rt_map_values(map);
    release_obj(value); // Map and Values should now be the only owners.

    rt_map_clear(map);
    assert(g_finalizer_calls == 0);
    rt_seq_clear(values);
    assert(g_finalizer_calls == 1);

    rt_string_unref(key);
    release_obj(values);
    release_obj(map);
}

static void test_frozenmap_values_snapshot_retains_values() {
    void *keys = rt_seq_new();
    void *vals = rt_seq_new();
    rt_string key = make_str("key");
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_seq_push(keys, key);
    rt_seq_push(vals, value);
    void *fm = rt_frozenmap_from_seqs(keys, vals);
    void *values = rt_frozenmap_values(fm);
    release_obj(value); // FrozenMap and Values should now be the only owners.

    release_obj(fm);
    assert(g_finalizer_calls == 0);
    rt_seq_clear(values);
    assert(g_finalizer_calls == 1);

    rt_string_unref(key);
    release_obj(values);
    release_obj(vals);
    release_obj(keys);
}

static void test_intmap_values_snapshot_retains_values() {
    void *map = rt_intmap_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_intmap_set(map, 7, value);
    void *values = rt_intmap_values(map);
    release_obj(value); // IntMap and Values should now be the only owners.

    rt_intmap_clear(map);
    assert(g_finalizer_calls == 0);
    rt_seq_clear(values);
    assert(g_finalizer_calls == 1);

    release_obj(values);
    release_obj(map);
}

} // namespace

int main() {
    test_collection_class_ids_are_specific();
    test_map_values_snapshot_retains_values();
    test_frozenmap_values_snapshot_retains_values();
    test_intmap_values_snapshot_retains_values();
    return 0;
}
