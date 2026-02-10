//===----------------------------------------------------------------------===//
// RTIterTests.cpp - Tests for rt_iter (unified collection iterator)
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "rt_internal.h"
#include "rt_iter.h"
#include "rt_seq.h"
#include "rt_list.h"
#include "rt_deque.h"
#include "rt_map.h"
#include "rt_set.h"
#include "rt_ring.h"
#include "rt_object.h"
#include "rt_string.h"

void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}
}

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
    } else { \
        tests_passed++; \
    } \
} while(0)

static void *make_obj() {
    return rt_obj_new_i64(0, 8);
}

//=============================================================================
// Null safety
//=============================================================================

static void test_null_safety() {
    ASSERT(rt_iter_from_seq(NULL) == NULL, "from_seq(null) = null");
    ASSERT(rt_iter_from_list(NULL) == NULL, "from_list(null) = null");
    ASSERT(rt_iter_from_deque(NULL) == NULL, "from_deque(null) = null");
    ASSERT(rt_iter_from_map_keys(NULL) == NULL, "from_map_keys(null) = null");
    ASSERT(rt_iter_from_map_values(NULL) == NULL, "from_map_values(null) = null");
    ASSERT(rt_iter_from_set(NULL) == NULL, "from_set(null) = null");
    ASSERT(rt_iter_from_ring(NULL) == NULL, "from_ring(null) = null");

    ASSERT(rt_iter_has_next(NULL) == 0, "has_next(null) = 0");
    ASSERT(rt_iter_next(NULL) == NULL, "next(null) = null");
    ASSERT(rt_iter_peek(NULL) == NULL, "peek(null) = null");
    ASSERT(rt_iter_index(NULL) == 0, "index(null) = 0");
    ASSERT(rt_iter_count(NULL) == 0, "count(null) = 0");
    ASSERT(rt_iter_skip(NULL, 5) == 0, "skip(null) = 0");
    rt_iter_reset(NULL); // should not crash
}

//=============================================================================
// Seq iterator tests
//=============================================================================

static void test_iter_from_seq() {
    void *seq = rt_seq_new();
    void *a = make_obj();
    void *b = make_obj();
    void *c = make_obj();
    rt_seq_push(seq, a);
    rt_seq_push(seq, b);
    rt_seq_push(seq, c);

    void *it = rt_iter_from_seq(seq);
    ASSERT(it != NULL, "iter from seq not null");
    ASSERT(rt_iter_count(it) == 3, "count = 3");
    ASSERT(rt_iter_index(it) == 0, "index = 0 initially");
    ASSERT(rt_iter_has_next(it) == 1, "has_next = 1");

    // Peek doesn't advance
    void *p = rt_iter_peek(it);
    ASSERT(p == a, "peek returns first element");
    ASSERT(rt_iter_index(it) == 0, "peek doesn't advance");

    // Next advances
    ASSERT(rt_iter_next(it) == a, "next returns first");
    ASSERT(rt_iter_index(it) == 1, "index = 1 after first next");
    ASSERT(rt_iter_next(it) == b, "next returns second");
    ASSERT(rt_iter_next(it) == c, "next returns third");

    ASSERT(rt_iter_has_next(it) == 0, "has_next = 0 after exhausted");
    ASSERT(rt_iter_next(it) == NULL, "next returns null when exhausted");
}

static void test_iter_reset() {
    void *seq = rt_seq_new();
    void *a = make_obj();
    void *b = make_obj();
    rt_seq_push(seq, a);
    rt_seq_push(seq, b);

    void *it = rt_iter_from_seq(seq);
    rt_iter_next(it);
    rt_iter_next(it);
    ASSERT(rt_iter_has_next(it) == 0, "exhausted");

    rt_iter_reset(it);
    ASSERT(rt_iter_index(it) == 0, "index reset to 0");
    ASSERT(rt_iter_has_next(it) == 1, "has_next after reset");
    ASSERT(rt_iter_next(it) == a, "first element after reset");
}

static void test_iter_skip() {
    void *seq = rt_seq_new();
    void *objs[5];
    int i;
    for (i = 0; i < 5; i++) {
        objs[i] = make_obj();
        rt_seq_push(seq, objs[i]);
    }

    void *it = rt_iter_from_seq(seq);

    int64_t skipped = rt_iter_skip(it, 3);
    ASSERT(skipped == 3, "skipped 3");
    ASSERT(rt_iter_index(it) == 3, "index = 3 after skip");
    ASSERT(rt_iter_next(it) == objs[3], "next after skip returns element 3");

    int64_t skipped2 = rt_iter_skip(it, 100);
    ASSERT(skipped2 == 1, "only 1 remaining to skip");
    ASSERT(rt_iter_has_next(it) == 0, "exhausted after skip past end");
}

static void test_iter_to_seq() {
    void *seq = rt_seq_new();
    void *a = make_obj();
    void *b = make_obj();
    void *c = make_obj();
    rt_seq_push(seq, a);
    rt_seq_push(seq, b);
    rt_seq_push(seq, c);

    void *it = rt_iter_from_seq(seq);
    rt_iter_next(it); // skip first

    void *collected = rt_iter_to_seq(it);
    ASSERT(collected != NULL, "to_seq returns seq");
    ASSERT(rt_seq_len(collected) == 2, "collected 2 remaining");
    ASSERT(rt_seq_get(collected, 0) == b, "first collected = b");
    ASSERT(rt_seq_get(collected, 1) == c, "second collected = c");
}

//=============================================================================
// List iterator tests
//=============================================================================

static void test_iter_from_list() {
    void *list = rt_ns_list_new();
    void *a = make_obj();
    void *b = make_obj();
    rt_list_push(list, a);
    rt_list_push(list, b);

    void *it = rt_iter_from_list(list);
    ASSERT(it != NULL, "iter from list not null");
    ASSERT(rt_iter_count(it) == 2, "list count = 2");
    ASSERT(rt_iter_next(it) == a, "list first = a");
    ASSERT(rt_iter_next(it) == b, "list second = b");
    ASSERT(rt_iter_has_next(it) == 0, "list exhausted");
}

//=============================================================================
// Deque iterator tests
//=============================================================================

static void test_iter_from_deque() {
    void *dq = rt_deque_new();
    void *a = make_obj();
    void *b = make_obj();
    rt_deque_push_back(dq, a);
    rt_deque_push_back(dq, b);

    void *it = rt_iter_from_deque(dq);
    ASSERT(it != NULL, "iter from deque not null");
    ASSERT(rt_iter_count(it) == 2, "deque count = 2");
    ASSERT(rt_iter_next(it) == a, "deque first = a");
    ASSERT(rt_iter_next(it) == b, "deque second = b");
}

//=============================================================================
// Map iterator tests
//=============================================================================

static void test_iter_from_map_keys() {
    void *map = rt_map_new();
    void *v1 = make_obj();
    void *v2 = make_obj();
    rt_string k1 = rt_string_from_bytes("alpha", 5);
    rt_string k2 = rt_string_from_bytes("beta", 4);
    rt_map_set(map, k1, v1);
    rt_map_set(map, k2, v2);

    void *it = rt_iter_from_map_keys(map);
    ASSERT(it != NULL, "iter from map keys not null");
    ASSERT(rt_iter_count(it) == 2, "map keys count = 2");

    int found_alpha = 0, found_beta = 0;
    while (rt_iter_has_next(it)) {
        void *key = rt_iter_next(it);
        if (key) {
            const char *s = rt_string_cstr((rt_string)key);
            if (s && strcmp(s, "alpha") == 0) found_alpha = 1;
            if (s && strcmp(s, "beta") == 0) found_beta = 1;
        }
    }
    ASSERT(found_alpha, "found key alpha");
    ASSERT(found_beta, "found key beta");
}

static void test_iter_from_map_values() {
    void *map = rt_map_new();
    void *v1 = make_obj();
    void *v2 = make_obj();
    rt_string k1 = rt_string_from_bytes("a", 1);
    rt_string k2 = rt_string_from_bytes("b", 1);
    rt_map_set(map, k1, v1);
    rt_map_set(map, k2, v2);

    void *it = rt_iter_from_map_values(map);
    ASSERT(it != NULL, "iter from map values not null");
    ASSERT(rt_iter_count(it) == 2, "map values count = 2");

    int found_v1 = 0, found_v2 = 0;
    while (rt_iter_has_next(it)) {
        void *val = rt_iter_next(it);
        if (val == v1) found_v1 = 1;
        if (val == v2) found_v2 = 1;
    }
    ASSERT(found_v1, "found value v1");
    ASSERT(found_v2, "found value v2");
}

//=============================================================================
// Set iterator tests
//=============================================================================

static void test_iter_from_set() {
    void *set = rt_set_new();
    void *a = make_obj();
    void *b = make_obj();
    void *c = make_obj();
    rt_set_put(set, a);
    rt_set_put(set, b);
    rt_set_put(set, c);

    void *it = rt_iter_from_set(set);
    ASSERT(it != NULL, "iter from set not null");
    ASSERT(rt_iter_count(it) == 3, "set count = 3");

    int count = 0;
    while (rt_iter_has_next(it)) {
        rt_iter_next(it);
        count++;
    }
    ASSERT(count == 3, "iterated 3 set items");
}

//=============================================================================
// Ring iterator tests
//=============================================================================

static void test_iter_from_ring() {
    void *ring = rt_ring_new(4);
    void *a = make_obj();
    void *b = make_obj();
    rt_ring_push(ring, a);
    rt_ring_push(ring, b);

    void *it = rt_iter_from_ring(ring);
    ASSERT(it != NULL, "iter from ring not null");
    ASSERT(rt_iter_count(it) == 2, "ring count = 2");
    ASSERT(rt_iter_next(it) == a, "ring first = a");
    ASSERT(rt_iter_next(it) == b, "ring second = b");
}

//=============================================================================
// Empty collection tests
//=============================================================================

static void test_iter_empty_seq() {
    void *seq = rt_seq_new();
    void *it = rt_iter_from_seq(seq);
    ASSERT(rt_iter_count(it) == 0, "empty seq count = 0");
    ASSERT(rt_iter_has_next(it) == 0, "empty seq has_next = 0");
    ASSERT(rt_iter_next(it) == NULL, "empty seq next = null");

    void *collected = rt_iter_to_seq(it);
    ASSERT(rt_seq_len(collected) == 0, "to_seq from empty = empty seq");
}

int main() {
    test_null_safety();
    test_iter_from_seq();
    test_iter_reset();
    test_iter_skip();
    test_iter_to_seq();
    test_iter_from_list();
    test_iter_from_deque();
    test_iter_from_map_keys();
    test_iter_from_map_values();
    test_iter_from_set();
    test_iter_from_ring();
    test_iter_empty_seq();

    printf("Iterator tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
