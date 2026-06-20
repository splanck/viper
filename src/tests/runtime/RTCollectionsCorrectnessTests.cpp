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
#include "rt_box.h"
#include "rt_collection_ids.h"
#include "rt_convert_coll.h"
#include "rt_countmap.h"
#include "rt_defaultmap.h"
#include "rt_deque.h"
#include "rt_frozenmap.h"
#include "rt_frozenset.h"
#include "rt_internal.h"
#include "rt_intmap.h"
#include "rt_iter.h"
#include "rt_list.h"
#include "rt_lrucache.h"
#include "rt_map.h"
#include "rt_msgbus.h"
#include "rt_multimap.h"
#include "rt_numbuf.h"
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
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

static jmp_buf g_trap_jmp;
static bool g_trap_expected = false;
static int g_finalizer_calls = 0;

} // namespace

extern "C" void vm_trap(const char *msg) {
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}

namespace {

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

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            (void)(expr);                                                                          \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static void test_collection_class_ids_are_specific() {
    assert_class_id(rt_seq_new(), RT_SEQ_CLASS_ID);
    assert_class_id(rt_map_new(), RT_MAP_CLASS_ID);
    assert_class_id(rt_list_new(), RT_LIST_CLASS_ID);
    assert_class_id(rt_f64buf_new(0), RT_F64BUFFER_CLASS_ID);
    assert_class_id(rt_i64buf_new(0), RT_I64BUFFER_CLASS_ID);
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

static void test_runtime_class_ids_are_unique() {
    const int64_t ids[] = {
        RT_SEQ_CLASS_ID,         RT_LIST_CLASS_ID,
        RT_SET_CLASS_ID,         RT_STACK_CLASS_ID,
        RT_QUEUE_CLASS_ID,       RT_RING_CLASS_ID,
        RT_DEQUE_CLASS_ID,       RT_BAG_CLASS_ID,
        RT_ORDEREDMAP_CLASS_ID,  RT_TREEMAP_CLASS_ID,
        RT_FROZENMAP_CLASS_ID,   RT_FROZENSET_CLASS_ID,
        RT_SPARSEARRAY_CLASS_ID, RT_INTMAP_CLASS_ID,
        RT_DEFAULTMAP_CLASS_ID,  RT_MULTIMAP_CLASS_ID,
        RT_LRUCACHE_CLASS_ID,    RT_TRIE_CLASS_ID,
        RT_BIMAP_CLASS_ID,       RT_BITSET_CLASS_ID,
        RT_BLOOMFILTER_CLASS_ID, RT_COUNTMAP_CLASS_ID,
        RT_PQUEUE_CLASS_ID,      RT_ITERATOR_CLASS_ID,
        RT_SORTEDSET_CLASS_ID,   RT_UNIONFIND_CLASS_ID,
        RT_WEAKMAP_CLASS_ID,     RT_MAP_CLASS_ID,
        RT_F64BUFFER_CLASS_ID,   RT_I64BUFFER_CLASS_ID,
        RT_MSGBUS_CLASS_ID,      RT_MSGBUS_CALLBACK_CLASS_ID,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        for (size_t j = i + 1; j < sizeof(ids) / sizeof(ids[0]); j++)
            assert(ids[i] != ids[j]);
    }
}

static void test_specialized_collections_reject_wrong_runtime_class() {
    void *wrong = rt_seq_new();

    EXPECT_TRAP(rt_countmap_len(wrong));
    EXPECT_TRAP(rt_multimap_len(wrong));
    EXPECT_TRAP(rt_intmap_len(wrong));
    EXPECT_TRAP(rt_sparse_len(wrong));
    EXPECT_TRAP(rt_defaultmap_len(wrong));
    EXPECT_TRAP(rt_orderedmap_len(wrong));
    EXPECT_TRAP(rt_bimap_len(wrong));
    EXPECT_TRAP(rt_lrucache_len(wrong));
    EXPECT_TRAP(rt_frozenset_len(wrong));
    EXPECT_TRAP(rt_weakmap_len(wrong));
    EXPECT_TRAP(rt_f64buf_len(wrong));
    EXPECT_TRAP(rt_i64buf_len(wrong));

    release_obj(wrong);
}

static void test_list_get_returns_owned_reference() {
    void *list = rt_list_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_list_push(list, value);
    release_obj(value); // List now owns the only reference.
    void *got = rt_list_get(list, 0);

    rt_list_clear(list);
    assert(g_finalizer_calls == 0);
    release_obj(got);
    assert(g_finalizer_calls == 1);

    release_obj(list);
}

static void test_deque_peek_and_get_return_owned_references() {
    void *deque = rt_deque_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_deque_push_back(deque, value);
    release_obj(value); // Deque now owns the only reference.
    void *peeked = rt_deque_peek_front(deque);
    void *got = rt_deque_get(deque, 0);

    rt_deque_clear(deque);
    assert(g_finalizer_calls == 0);
    release_obj(peeked);
    assert(g_finalizer_calls == 0);
    release_obj(got);
    assert(g_finalizer_calls == 1);

    release_obj(deque);
}

static void test_owned_seq_pop_and_remove_transfer_reference() {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    void *a = new_obj();
    void *b = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(a, count_finalizer);
    rt_obj_set_finalizer(b, count_finalizer);

    rt_seq_push(seq, a);
    rt_seq_push(seq, b);
    release_obj(a);
    release_obj(b);

    void *removed = rt_seq_remove(seq, 0);
    void *popped = rt_seq_pop(seq);
    assert(g_finalizer_calls == 0);
    release_obj(removed);
    assert(g_finalizer_calls == 1);
    release_obj(popped);
    assert(g_finalizer_calls == 2);

    release_obj(seq);
}

static void test_multimap_get_first_returns_owned_reference() {
    void *mm = rt_multimap_new();
    rt_string key = make_str("key");
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_multimap_put(mm, key, value);
    release_obj(value); // MultiMap now owns the only reference.
    void *first = rt_multimap_get_first(mm, key);

    rt_multimap_clear(mm);
    assert(g_finalizer_calls == 0);
    release_obj(first);
    assert(g_finalizer_calls == 1);

    rt_string_unref(key);
    release_obj(mm);
}

static void test_heap_retains_values_and_transfers_on_pop() {
    void *heap = rt_pqueue_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_pqueue_push(heap, 10, value);
    release_obj(value); // Heap now owns the only reference.
    void *popped = rt_pqueue_pop(heap);

    release_obj(heap);
    assert(g_finalizer_calls == 0);
    release_obj(popped);
    assert(g_finalizer_calls == 1);
}

static void test_heap_to_seq_returns_owned_snapshot() {
    void *heap = rt_pqueue_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_pqueue_push(heap, 10, value);
    release_obj(value); // Heap now owns the only reference.
    void *seq = rt_pqueue_to_seq(heap);

    rt_pqueue_clear(heap);
    assert(g_finalizer_calls == 0);
    rt_seq_clear(seq);
    assert(g_finalizer_calls == 1);

    release_obj(seq);
    release_obj(heap);
}

static void test_sortedset_accessors_return_stable_strings() {
    void *set = rt_sortedset_new();
    rt_string apple = make_str("apple");
    rt_sortedset_add(set, apple);

    rt_string first = rt_sortedset_first(set);
    rt_sortedset_clear(set);
    assert(strcmp(rt_string_cstr(first), "apple") == 0);

    rt_string_unref(first);
    rt_string_unref(apple);
    release_obj(set);
}

static void test_sortedset_null_start_range_includes_lowest_values() {
    void *set = rt_sortedset_new();
    rt_string a = make_str("a");
    rt_string b = make_str("b");
    rt_string c = make_str("c");

    rt_sortedset_add(set, c);
    rt_sortedset_add(set, a);
    rt_sortedset_add(set, b);

    void *range = rt_sortedset_range(set, nullptr, b);
    assert(rt_seq_len(range) == 2);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(range, 0)), "a") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(range, 1)), "b") == 0);

    release_obj(range);
    rt_string_unref(a);
    rt_string_unref(b);
    rt_string_unref(c);
    release_obj(set);
}

static void test_frozenmap_equals_uses_boxed_value_equality() {
    void *keys1 = rt_seq_new();
    void *vals1 = rt_seq_new();
    void *keys2 = rt_seq_new();
    void *vals2 = rt_seq_new();
    rt_string key1 = make_str("k");
    rt_string key2 = make_str("k");
    void *value1 = rt_box_i64(42);
    void *value2 = rt_box_i64(42);

    rt_seq_push(keys1, key1);
    rt_seq_push(vals1, value1);
    rt_seq_push(keys2, key2);
    rt_seq_push(vals2, value2);

    void *fm1 = rt_frozenmap_from_seqs(keys1, vals1);
    void *fm2 = rt_frozenmap_from_seqs(keys2, vals2);
    assert(rt_frozenmap_equals(fm1, fm2) == 1);

    release_obj(fm1);
    release_obj(fm2);
    release_obj(value1);
    release_obj(value2);
    rt_string_unref(key1);
    rt_string_unref(key2);
    release_obj(vals1);
    release_obj(vals2);
    release_obj(keys1);
    release_obj(keys2);
}

static void test_seq_to_bag_accepts_raw_and_boxed_strings() {
    void *seq = rt_seq_new();
    rt_string raw = make_str("raw");
    rt_string boxed_text = make_str("boxed");
    void *boxed = rt_box_str(boxed_text);

    rt_seq_push(seq, raw);
    rt_seq_push(seq, boxed);
    void *bag = rt_seq_to_bag(seq);

    assert(rt_bag_len(bag) == 2);
    assert(rt_bag_has(bag, raw) == 1);
    assert(rt_bag_has(bag, boxed_text) == 1);

    release_obj(bag);
    release_obj(boxed);
    rt_string_unref(raw);
    rt_string_unref(boxed_text);
    release_obj(seq);
}

static void test_boxed_nan_equality_is_hash_compatible() {
    double nan = std::numeric_limits<double>::quiet_NaN();
    void *a = rt_box_f64(nan);
    void *b = rt_box_f64(nan);
    void *set = rt_set_new();

    assert(rt_box_equal(a, b) == 1);
    assert(rt_set_add(set, a) == 1);
    assert(rt_set_add(set, b) == 0);
    assert(rt_set_has(set, b) == 1);

    release_obj(set);
    release_obj(a);
    release_obj(b);
}

static void test_trie_with_long_prefix() {
    char key_buf[5001];
    memset(key_buf, 'a', sizeof(key_buf));
    key_buf[5000] = '\0';
    rt_string key = rt_string_from_bytes(key_buf, 5000);
    void *trie = rt_trie_new();
    void *value = new_obj();

    rt_trie_set(trie, key, value);
    void *matches = rt_trie_with_prefix(trie, key);
    assert(rt_seq_len(matches) == 1);
    void *clone = rt_trie_clone(trie);
    assert(rt_trie_has(clone, key) == 1);
    void *keys = rt_trie_keys(clone);
    assert(rt_seq_len(keys) == 1);
    rt_string cloned_key = (rt_string)rt_seq_get(keys, 0);
    assert(rt_str_len(cloned_key) == 5000);

    release_obj(keys);
    release_obj(clone);
    release_obj(matches);
    release_obj(value);
    release_obj(trie);
    rt_string_unref(key);
}

static void test_list_pop_and_remove_at_leave_empty_list_reusable() {
    void *list = rt_list_new();
    void *first = new_obj();
    void *second = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(first, count_finalizer);
    rt_obj_set_finalizer(second, count_finalizer);

    rt_list_push(list, first);
    release_obj(first);
    void *popped = rt_list_pop(list);
    assert(rt_list_len(list) == 0);
    assert(g_finalizer_calls == 0);
    release_obj(popped);
    assert(g_finalizer_calls == 1);

    rt_list_push(list, second);
    release_obj(second);
    rt_list_remove_at(list, 0);
    assert(rt_list_len(list) == 0);
    assert(g_finalizer_calls == 2);

    release_obj(list);
}

static void test_bloomfilter_self_merge_preserves_count() {
    void *filter = rt_bloomfilter_new(16, 0.01);
    rt_string key = make_str("alpha");
    rt_bloomfilter_add(filter, key);
    assert(rt_bloomfilter_count(filter) == 1);
    assert(rt_bloomfilter_merge(filter, filter) == 1);
    assert(rt_bloomfilter_count(filter) == 1);
    rt_string_unref(key);
    release_obj(filter);
}

static void test_stack_to_seq_releases_borrowed_pop_temps() {
    void *stack = rt_stack_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_stack_push(stack, value); // Borrowed stack mode.
    void *seq = rt_stack_to_seq(stack);

    release_obj(stack);
    release_obj(seq);
    assert(g_finalizer_calls == 0);
    release_obj(value);
    assert(g_finalizer_calls == 1);
}

static void test_queue_to_seq_releases_borrowed_pop_temps() {
    void *queue = rt_queue_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_queue_push(queue, value); // Borrowed queue mode.
    void *seq = rt_queue_to_seq(queue);

    release_obj(queue);
    release_obj(seq);
    assert(g_finalizer_calls == 0);
    release_obj(value);
    assert(g_finalizer_calls == 1);
}

static void test_iterator_deque_snapshot_releases_get_temps() {
    void *deque = rt_deque_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_deque_push_back(deque, value);
    release_obj(value); // Deque now owns the only reference.

    void *iter = rt_iter_from_deque(deque);
    void *next = rt_iter_next(iter);

    release_obj(deque);
    release_obj(iter);
    assert(g_finalizer_calls == 0);
    release_obj(next);
    assert(g_finalizer_calls == 1);
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

static void test_empty_seq_results_keep_owned_mode() {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);

    void *empty_slice = rt_seq_slice(seq, 0, 0);
    void *empty_take = rt_seq_take(seq, 0);
    void *empty_drop = rt_seq_drop(seq, 10);
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_seq_push(empty_slice, value);
    rt_seq_push(empty_take, value);
    rt_seq_push(empty_drop, value);
    release_obj(value);
    assert(g_finalizer_calls == 0);

    rt_seq_clear(empty_slice);
    assert(g_finalizer_calls == 0);
    rt_seq_clear(empty_take);
    assert(g_finalizer_calls == 0);
    rt_seq_clear(empty_drop);
    assert(g_finalizer_calls == 1);

    release_obj(empty_drop);
    release_obj(empty_take);
    release_obj(empty_slice);
    release_obj(seq);
}

static void test_multimap_missing_get_returns_owned_seq() {
    void *mm = rt_multimap_new();
    rt_string missing = make_str("missing");
    void *values = rt_multimap_get(mm, missing);
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_seq_push(values, value);
    release_obj(value);
    assert(g_finalizer_calls == 0);

    rt_seq_clear(values);
    assert(g_finalizer_calls == 1);

    rt_string_unref(missing);
    release_obj(values);
    release_obj(mm);
}

static void test_iterator_null_to_seq_returns_owned_seq() {
    void *seq = rt_iter_to_seq(nullptr);
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_seq_push(seq, value);
    release_obj(value);
    assert(g_finalizer_calls == 0);

    rt_seq_clear(seq);
    assert(g_finalizer_calls == 1);

    release_obj(seq);
}

} // namespace

int main() {
    test_collection_class_ids_are_specific();
    test_runtime_class_ids_are_unique();
    test_specialized_collections_reject_wrong_runtime_class();
    test_list_get_returns_owned_reference();
    test_deque_peek_and_get_return_owned_references();
    test_owned_seq_pop_and_remove_transfer_reference();
    test_multimap_get_first_returns_owned_reference();
    test_heap_retains_values_and_transfers_on_pop();
    test_heap_to_seq_returns_owned_snapshot();
    test_sortedset_accessors_return_stable_strings();
    test_sortedset_null_start_range_includes_lowest_values();
    test_frozenmap_equals_uses_boxed_value_equality();
    test_seq_to_bag_accepts_raw_and_boxed_strings();
    test_boxed_nan_equality_is_hash_compatible();
    test_trie_with_long_prefix();
    test_list_pop_and_remove_at_leave_empty_list_reusable();
    test_bloomfilter_self_merge_preserves_count();
    test_stack_to_seq_releases_borrowed_pop_temps();
    test_queue_to_seq_releases_borrowed_pop_temps();
    test_iterator_deque_snapshot_releases_get_temps();
    test_map_values_snapshot_retains_values();
    test_frozenmap_values_snapshot_retains_values();
    test_intmap_values_snapshot_retains_values();
    test_empty_seq_results_keep_owned_mode();
    test_multimap_missing_get_returns_owned_seq();
    test_iterator_null_to_seq_returns_owned_seq();
    return 0;
}
