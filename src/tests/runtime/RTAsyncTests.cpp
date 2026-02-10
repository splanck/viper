//===----------------------------------------------------------------------===//
// RTAsyncTests.cpp - Tests for rt_async (async task combinators)
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" {
#include "rt_internal.h"
#include "rt_async.h"
#include "rt_cancellation.h"
#include "rt_future.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_threads.h"

void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}
}

static void *make_obj() {
    return rt_obj_new_i64(0, 8);
}

//=============================================================================
// Callbacks for testing
//=============================================================================

extern "C" {

static void *identity_cb(void *arg) {
    return arg;
}

static void *slow_cb(void *arg) {
    rt_thread_sleep(50);
    return arg;
}

static void *add_one_mapper(void *val, void *arg) {
    (void)arg;
    // Return a new object (simulating a transformation)
    return make_obj();
}

static void *cancellable_cb(void *arg, void *token) {
    (void)arg;
    // Simulate work that checks cancellation
    int i;
    for (i = 0; i < 50; i++) {
        if (token && rt_cancellation_is_cancelled(token))
            return NULL;
        rt_thread_sleep(2);
    }
    return make_obj();
}

} // extern "C"

//=============================================================================
// rt_async_run tests
//=============================================================================

static void test_async_run_basic() {
    void *val = make_obj();
    void *future = rt_async_run((void *)identity_cb, val);
    assert(future != NULL);

    void *result = rt_future_get(future);
    assert(result == val);
}

static void test_async_run_null_arg() {
    void *future = rt_async_run((void *)identity_cb, NULL);
    assert(future != NULL);

    void *result = rt_future_get(future);
    assert(result == NULL);
}

static void test_async_run_multiple() {
    const int N = 5;
    void *vals[5];
    void *futures[5];
    int i;

    for (i = 0; i < N; i++) {
        vals[i] = make_obj();
        futures[i] = rt_async_run((void *)identity_cb, vals[i]);
    }

    for (i = 0; i < N; i++) {
        void *result = rt_future_get(futures[i]);
        assert(result == vals[i]);
    }
}

//=============================================================================
// rt_async_delay tests
//=============================================================================

static void test_async_delay() {
    auto start = std::chrono::steady_clock::now();
    void *future = rt_async_delay(50);
    assert(future != NULL);
    assert(rt_future_is_done(future) == 0 || 1); // May or may not be done yet

    rt_future_wait(future);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    assert(elapsed >= 30); // Allow some variance
}

static void test_async_delay_zero() {
    void *future = rt_async_delay(0);
    assert(future != NULL);
    rt_future_wait(future);
    assert(rt_future_is_done(future) == 1);
}

static void test_async_delay_negative() {
    void *future = rt_async_delay(-100);
    assert(future != NULL);
    rt_future_wait(future);
    assert(rt_future_is_done(future) == 1);
}

//=============================================================================
// rt_async_all tests
//=============================================================================

static void test_async_all_basic() {
    void *futures = rt_seq_new();
    void *val1 = make_obj();
    void *val2 = make_obj();
    void *val3 = make_obj();

    rt_seq_push(futures, rt_async_run((void *)identity_cb, val1));
    rt_seq_push(futures, rt_async_run((void *)identity_cb, val2));
    rt_seq_push(futures, rt_async_run((void *)identity_cb, val3));

    void *all_future = rt_async_all(futures);
    assert(all_future != NULL);

    void *results = rt_future_get(all_future);
    assert(results != NULL);
    assert(rt_seq_len(results) == 3);
    assert(rt_seq_get(results, 0) == val1);
    assert(rt_seq_get(results, 1) == val2);
    assert(rt_seq_get(results, 2) == val3);
}

static void test_async_all_empty() {
    void *futures = rt_seq_new();
    void *all_future = rt_async_all(futures);
    assert(all_future != NULL);

    void *results = rt_future_get(all_future);
    assert(results != NULL);
    assert(rt_seq_len(results) == 0);
}

static void test_async_all_null() {
    void *all_future = rt_async_all(NULL);
    assert(all_future != NULL);

    void *results = rt_future_get(all_future);
    assert(results != NULL);
    assert(rt_seq_len(results) == 0);
}

//=============================================================================
// rt_async_any tests
//=============================================================================

static void test_async_any_basic() {
    void *futures = rt_seq_new();
    void *fast_val = make_obj();

    // One fast, two slow
    rt_seq_push(futures, rt_async_run((void *)identity_cb, fast_val));
    rt_seq_push(futures, rt_async_run((void *)slow_cb, make_obj()));
    rt_seq_push(futures, rt_async_run((void *)slow_cb, make_obj()));

    void *any_future = rt_async_any(futures);
    assert(any_future != NULL);

    void *result = rt_future_get(any_future);
    // The fast one should complete first
    assert(result == fast_val);
}

static void test_async_any_empty() {
    void *futures = rt_seq_new();
    void *any_future = rt_async_any(futures);
    assert(any_future != NULL);

    // Should resolve with error
    rt_future_wait(any_future);
    assert(rt_future_is_error(any_future) == 1);
}

//=============================================================================
// rt_async_map tests
//=============================================================================

static void test_async_map_basic() {
    void *val = make_obj();
    void *source = rt_async_run((void *)identity_cb, val);

    void *mapped = rt_async_map(source, (void *)add_one_mapper, NULL);
    assert(mapped != NULL);

    void *result = rt_future_get(mapped);
    assert(result != NULL);
    assert(result != val); // Should be a new object
}

static void test_async_map_chained() {
    void *val = make_obj();
    void *f1 = rt_async_run((void *)identity_cb, val);
    void *f2 = rt_async_map(f1, (void *)add_one_mapper, NULL);
    void *f3 = rt_async_map(f2, (void *)add_one_mapper, NULL);

    void *result = rt_future_get(f3);
    assert(result != NULL);
}

//=============================================================================
// rt_async_run_cancellable tests
//=============================================================================

static void test_cancellable_normal() {
    void *token = rt_cancellation_new();
    void *future = rt_async_run_cancellable((void *)cancellable_cb, NULL, token);
    assert(future != NULL);

    // Let it complete normally
    rt_future_wait(future);
    assert(rt_future_is_done(future) == 1);
    assert(rt_future_is_error(future) == 0);
}

static void test_cancellable_cancelled() {
    void *token = rt_cancellation_new();
    void *future = rt_async_run_cancellable((void *)cancellable_cb, NULL, token);
    assert(future != NULL);

    // Cancel after a short delay
    rt_thread_sleep(10);
    rt_cancellation_cancel(token);

    rt_future_wait(future);
    assert(rt_future_is_done(future) == 1);
    assert(rt_future_is_error(future) == 1);
}

static void test_cancellable_null_token() {
    // Should work like a normal async run
    void *future = rt_async_run_cancellable((void *)cancellable_cb, NULL, NULL);
    assert(future != NULL);

    rt_future_wait(future);
    assert(rt_future_is_done(future) == 1);
    assert(rt_future_is_error(future) == 0);
}

//=============================================================================
// Timing tests
//=============================================================================

static void test_async_runs_concurrently() {
    auto start = std::chrono::steady_clock::now();

    // Launch 5 tasks each sleeping 50ms
    void *futures[5];
    int i;
    for (i = 0; i < 5; i++)
        futures[i] = rt_async_run((void *)slow_cb, make_obj());

    // Wait for all
    for (i = 0; i < 5; i++)
        rt_future_wait(futures[i]);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // If truly concurrent, should take ~50ms not ~250ms
    // Allow generous margin for CI
    assert(elapsed < 200);
}

int main() {
    test_async_run_basic();
    test_async_run_null_arg();
    test_async_run_multiple();
    test_async_delay();
    test_async_delay_zero();
    test_async_delay_negative();
    test_async_all_basic();
    test_async_all_empty();
    test_async_all_null();
    test_async_any_basic();
    test_async_any_empty();
    test_async_map_basic();
    test_async_map_chained();
    test_cancellable_normal();
    test_cancellable_cancelled();
    test_cancellable_null_token();
    test_async_runs_concurrently();

    printf("Async tests: all passed\n");
    return 0;
}
