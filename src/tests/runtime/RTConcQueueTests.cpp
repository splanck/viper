//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_concqueue.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_string.h"
#include "rt_threads.h"
#include "rt_trap.h"

#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" void rt_trap_set_recovery(jmp_buf *buf);
extern "C" void rt_trap_clear_recovery(void);

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void *make_obj() {
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

template <typename Fn> static bool expect_trap(Fn fn) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        fn();
        rt_trap_clear_recovery();
        return false;
    }
    rt_trap_clear_recovery();
    return true;
}

static void test_new() {
    void *q = rt_concqueue_new();
    assert(q != NULL);
    assert(rt_concqueue_len(q) == 0);
    assert(rt_concqueue_is_empty(q) == 1);
}

static void test_enqueue_dequeue() {
    void *q = rt_concqueue_new();
    rt_string v1 = make_str("first");
    rt_string v2 = make_str("second");

    rt_concqueue_enqueue(q, v1);
    rt_concqueue_enqueue(q, v2);

    assert(rt_concqueue_len(q) == 2);
    assert(rt_concqueue_try_dequeue(q) == v1);
    assert(rt_concqueue_try_dequeue(q) == v2);
    assert(rt_concqueue_len(q) == 0);
}

static void test_try_dequeue_empty() {
    void *q = rt_concqueue_new();
    assert(rt_concqueue_try_dequeue(q) == NULL);
    void *empty = rt_concqueue_try_dequeue_option(q);
    assert(rt_option_is_none(empty) == 1);

    rt_concqueue_enqueue(q, NULL);
    void *null_value = rt_concqueue_try_dequeue_option(q);
    assert(rt_option_is_some(null_value) == 1);
    assert(rt_option_unwrap(null_value) == NULL);
}

static void test_peek() {
    void *q = rt_concqueue_new();
    rt_string v = make_str("peeked");
    rt_concqueue_enqueue(q, v);

    assert(rt_concqueue_peek(q) == v);
    assert(rt_concqueue_len(q) == 1); // Still there
}

static void test_enqueue_retain_overflow_cleans_up() {
    void *q = rt_concqueue_new();
    rt_heap_hdr_t *queue_hdr = rt_heap_hdr(q);
    void *item = make_obj();
    rt_heap_hdr_t *item_hdr = rt_heap_hdr(item);
    item_hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    assert(expect_trap([&]() { rt_concqueue_enqueue(q, item); }));
    assert(queue_hdr->refcnt == 1);
    assert(rt_concqueue_len(q) == 0);

    item_hdr->refcnt = 1;
}

static void test_peek_retain_overflow_unlocks_queue() {
    void *q = rt_concqueue_new();
    void *item = make_obj();
    rt_concqueue_enqueue(q, item);

    rt_heap_hdr_t *item_hdr = rt_heap_hdr(item);
    item_hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;
    assert(expect_trap([&]() { (void)rt_concqueue_peek(q); }));
    assert(rt_concqueue_len(q) == 1);

    item_hdr->refcnt = 2;
    assert(rt_concqueue_try_dequeue(q) == item);
}

static void test_clear() {
    void *q = rt_concqueue_new();
    rt_concqueue_enqueue(q, make_str("a"));
    rt_concqueue_enqueue(q, make_str("b"));
    rt_concqueue_enqueue(q, make_str("c"));

    rt_concqueue_clear(q);
    assert(rt_concqueue_len(q) == 0);
    assert(rt_concqueue_try_dequeue(q) == NULL);
}

static void test_timeout_empty() {
    void *q = rt_concqueue_new();
    // Should return NULL after ~10ms timeout
    void *result = rt_concqueue_dequeue_timeout(q, 10);
    assert(result == NULL);
}

static void test_huge_timeout_immediate_dequeue() {
    void *q = rt_concqueue_new();
    rt_string v = make_str("ready");
    rt_concqueue_enqueue(q, v);

    assert(rt_concqueue_dequeue_timeout(q, INT64_MAX) == v);
}

static void test_close_wakes_blocked_dequeue() {
    void *q = rt_concqueue_new();
    assert(rt_concqueue_get_is_closed(q) == 0);

    void *result = (void *)0x1;
    std::thread waiter([&]() { result = rt_concqueue_dequeue(q); });

    rt_thread_sleep(30);
    rt_concqueue_close(q);
    waiter.join();

    assert(rt_concqueue_get_is_closed(q) == 1);
    assert(result == NULL);
}

static void producer_fn(void *queue, int count) {
    for (int i = 0; i < count; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "item_%d", i);
        rt_concqueue_enqueue(queue, make_str(buf));
    }
}

static void test_concurrent_produce_consume() {
    void *q = rt_concqueue_new();
    const int N = 100;

    std::thread producer(producer_fn, q, N);

    // Consumer: dequeue all N items
    int received = 0;
    while (received < N) {
        void *item = rt_concqueue_dequeue_timeout(q, 500);
        if (item)
            received++;
    }
    producer.join();

    assert(received == N);
    assert(rt_concqueue_len(q) == 0);
}

static void test_null_safety() {
    assert(rt_concqueue_len(NULL) == 0);
    assert(rt_concqueue_is_empty(NULL) == 1);
    assert(rt_concqueue_try_dequeue(NULL) == NULL);
    assert(rt_concqueue_peek(NULL) == NULL);
    assert(rt_concqueue_dequeue_timeout(NULL, 10) == NULL);
}

/// @brief Main.
int main() {
    test_new();
    test_enqueue_dequeue();
    test_try_dequeue_empty();
    test_peek();
    test_enqueue_retain_overflow_cleans_up();
    test_peek_retain_overflow_unlocks_queue();
    test_clear();
    test_timeout_empty();
    test_huge_timeout_immediate_dequeue();
    test_close_wakes_blocked_dequeue();
    test_concurrent_produce_consume();
    test_null_safety();
    return 0;
}
