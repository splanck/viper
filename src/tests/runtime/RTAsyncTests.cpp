//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTAsyncTests.cpp
// Purpose: Validate async task combinators, cancellation, traps, ownership,
//          and concurrent execution.
// Key invariants:
//   - Future aggregation preserves result order and propagates errors.
//   - Owned arguments and mapped values have balanced lifetimes.
//   - Concurrent tasks start independently and cancellation remains bounded.
// Ownership/Lifetime:
//   - Every managed future, sequence, and test value is released by its test.
// Links: src/runtime/threads/rt_async.c, src/runtime/threads/rt_future.c
//
//===----------------------------------------------------------------------===//

#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" {
#include "rt_async.h"
#include "rt_cancellation.h"
#include "rt_future.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_seq_internal.h"
#include "rt_string.h"
#include "rt_threads.h"

/// @brief Vm_trap.
void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}
}

static void *make_obj() {
    return rt_obj_new_i64(0, 8);
}

static std::atomic<int> g_precancel_calls{0};
static std::atomic<int> g_trap_calls{0};
static std::atomic<int64_t> g_owned_arg_len{0};
static std::atomic<int> g_concurrent_started{0};
static std::atomic<int> g_concurrent_active{0};
static std::atomic<int> g_concurrent_peak{0};
static std::atomic<int> g_concurrent_release{0};
static std::atomic<int> g_map_source_finalized{0};
static void *g_borrowed_async_result = nullptr;
static void *g_borrowed_map_result = nullptr;

//=============================================================================
// Callbacks for testing
//=============================================================================

extern "C" {
static void *identity_cb(void *arg) {
    return arg;
}

static void *new_obj_cb(void *arg) {
    (void)arg;
    return make_obj();
}

static void *borrowed_result_cb(void *arg) {
    (void)arg;
    return g_borrowed_async_result;
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

static void *borrowed_result_mapper(void *val, void *arg) {
    (void)val;
    (void)arg;
    return g_borrowed_map_result;
}

static void *trapping_mapper(void *val, void *arg) {
    (void)val;
    (void)arg;
    rt_trap("async map trap");
    return NULL;
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

static void *precancel_counting_cb(void *arg, void *token) {
    (void)arg;
    (void)token;
    g_precancel_calls.fetch_add(1);
    return make_obj();
}

static void *trap_cb(void *arg) {
    (void)arg;
    g_trap_calls.fetch_add(1);
    rt_trap("async trap");
    return NULL;
}

static void *owned_arg_observer_cb(void *arg) {
    rt_thread_sleep(20);
    g_owned_arg_len.store(rt_seq_len(arg), std::memory_order_release);
    return (void *)(intptr_t)42;
}

static void *owned_arg_passthrough_cb(void *arg) {
    rt_thread_sleep(20);
    g_owned_arg_len.store(rt_seq_len(arg), std::memory_order_release);
    return arg;
}

static void update_concurrent_peak(int active) {
    int peak = g_concurrent_peak.load(std::memory_order_relaxed);
    while (active > peak &&
           !g_concurrent_peak.compare_exchange_weak(
               peak, active, std::memory_order_release, std::memory_order_relaxed)) {
    }
}

static void *barrier_concurrent_cb(void *arg) {
    int active = g_concurrent_active.fetch_add(1, std::memory_order_acq_rel) + 1;
    update_concurrent_peak(active);
    g_concurrent_started.fetch_add(1, std::memory_order_acq_rel);
    while (!g_concurrent_release.load(std::memory_order_acquire))
        rt_thread_sleep(1);
    g_concurrent_active.fetch_sub(1, std::memory_order_acq_rel);
    return arg;
}

} // extern "C"

static void map_source_finalizer(void *obj) {
    (void)obj;
    g_map_source_finalized.fetch_add(1, std::memory_order_acq_rel);
}

static bool wait_for_started_count(int expected, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited++) {
        if (g_concurrent_started.load(std::memory_order_acquire) >= expected)
            return true;
        rt_thread_sleep(1);
    }
    return g_concurrent_started.load(std::memory_order_acquire) >= expected;
}

static void corrupt_seq_len(void *seq, int64_t len) {
    ((rt_seq_impl *)seq)->len = len;
}

//=============================================================================
// rt_async_run tests
//=============================================================================

static void test_async_run_basic() {
    void *val = make_obj();
    void *future = rt_async_run((void *)identity_cb, val);
    assert(future != NULL);

    rt_future_wait(future);
    // Since d954bee0c / dcb4c46e9 unified rt_promise_set / rt_promise_set_owned
    // retain semantics (UAF fix: worker VMs unwind before the caller reads the
    // Future, so the Future must retain the payload itself), every non-null
    // resolution sets owns_value = 1. This test previously expected == 0 under
    // the old "set() doesn't retain" contract; update to the current contract.
    assert(rt_future_value_is_owned(future) == 1);
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

static void test_async_run_owned_keeps_object_arg_alive() {
    g_owned_arg_len.store(-1, std::memory_order_release);
    void *arg = rt_seq_new();
    void *future = rt_async_run_owned((void *)owned_arg_observer_cb, arg);
    assert(future != NULL);

    if (rt_obj_release_check0(arg))
        rt_obj_free(arg);

    void *result = rt_future_get(future);
    assert((int64_t)(intptr_t)result == 42);
    assert(g_owned_arg_len.load(std::memory_order_acquire) == 0);
}

static void test_async_run_owned_passthrough_preserves_result() {
    g_owned_arg_len.store(-1, std::memory_order_release);
    void *arg = rt_seq_new();
    void *future = rt_async_run_owned((void *)owned_arg_passthrough_cb, arg);
    assert(future != NULL);

    if (rt_obj_release_check0(arg))
        rt_obj_free(arg);

    void *result = rt_future_get(future);
    assert(result != NULL);
    assert(g_owned_arg_len.load(std::memory_order_acquire) == 0);

    if (rt_obj_release_check0(future))
        rt_obj_free(future);

    assert(rt_seq_len(result) == 0);
    if (rt_obj_release_check0(result))
        rt_obj_free(result);
}

static void test_async_run_retains_callback_result() {
    void *future = rt_async_run((void *)new_obj_cb, NULL);
    assert(future != NULL);

    rt_future_wait(future);
    assert(rt_future_value_is_owned(future) == 1);

    void *result = rt_future_get(future);
    assert(result != NULL);
    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(result))
        rt_obj_free(result);
}

static void test_async_run_retains_borrowed_callback_result() {
    void *borrowed = rt_seq_new();
    g_borrowed_async_result = borrowed;

    void *future = rt_async_run((void *)borrowed_result_cb, NULL);
    assert(future != NULL);
    void *result = rt_future_get(future);
    assert(result == borrowed);

    if (rt_obj_release_check0(borrowed))
        rt_obj_free(borrowed);
    g_borrowed_async_result = nullptr;
    if (rt_obj_release_check0(future))
        rt_obj_free(future);

    assert(rt_seq_len(result) == 0);
    if (rt_obj_release_check0(result))
        rt_obj_free(result);
}

//=============================================================================
// rt_async_delay tests
//=============================================================================

static void test_async_delay() {
    auto start = std::chrono::steady_clock::now();
    void *future = rt_async_delay(50);
    assert(future != NULL);

    rt_future_wait(future);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       /// @brief Now.
                       std::chrono::steady_clock::now() - start)
                       .count();
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

static void test_async_all_rejects_huge_count() {
    void *futures = rt_seq_new();
    corrupt_seq_len(futures, INT64_MAX);

    void *all_future = rt_async_all(futures);
    assert(all_future != NULL);
    assert(rt_future_is_done(all_future) == 1);
    assert(rt_future_is_error(all_future) == 1);

    rt_string err = rt_future_get_error(all_future);
    assert(std::strstr(rt_string_cstr(err), "Async.All: futures count too large") != NULL);
    rt_string_unref(err);
}

static void test_async_all_short_circuits_on_error() {
    void *pending_promise = rt_promise_new();
    void *pending_future = rt_promise_get_future(pending_promise);
    void *error_promise = rt_promise_new();
    void *error_future = rt_promise_get_future(error_promise);
    void *futures = rt_seq_new();

    rt_seq_push(futures, pending_future);
    rt_seq_push(futures, error_future);

    void *all_future = rt_async_all(futures);
    assert(all_future != NULL);

    rt_promise_set_error(error_promise, rt_const_cstr("boom"));
    assert(rt_future_wait_for(all_future, 250) == 1);
    assert(rt_future_is_error(all_future) == 1);
    assert(strcmp(rt_string_cstr(rt_future_get_error(all_future)), "boom") == 0);

    if (rt_obj_release_check0(pending_future))
        rt_obj_free(pending_future);
    if (rt_obj_release_check0(pending_promise))
        rt_obj_free(pending_promise);
    if (rt_obj_release_check0(error_future))
        rt_obj_free(error_future);
    if (rt_obj_release_check0(error_promise))
        rt_obj_free(error_promise);
}

static void test_async_all_result_owns_values() {
    void *promise1 = rt_promise_new();
    void *future1 = rt_promise_get_future(promise1);
    void *promise2 = rt_promise_new();
    void *future2 = rt_promise_get_future(promise2);
    void *futures = rt_seq_new();
    void *value1 = rt_seq_new();
    void *value2 = rt_seq_new();

    rt_seq_push(futures, future1);
    rt_seq_push(futures, future2);
    void *all_future = rt_async_all(futures);
    assert(all_future != NULL);

    rt_promise_set_owned(promise1, value1);
    rt_promise_set_owned(promise2, value2);
    if (rt_obj_release_check0(value1))
        rt_obj_free(value1);
    if (rt_obj_release_check0(value2))
        rt_obj_free(value2);

    void *results = rt_future_get(all_future);
    assert(results != NULL);
    assert(rt_seq_get(results, 0) != NULL);
    assert(rt_seq_get(results, 1) != NULL);

    if (rt_obj_release_check0(future1))
        rt_obj_free(future1);
    if (rt_obj_release_check0(future2))
        rt_obj_free(future2);
    if (rt_obj_release_check0(promise1))
        rt_obj_free(promise1);
    if (rt_obj_release_check0(promise2))
        rt_obj_free(promise2);
    if (rt_obj_release_check0(all_future))
        rt_obj_free(all_future);
    if (rt_obj_release_check0(futures))
        rt_obj_free(futures);

    assert(rt_seq_len(rt_seq_get(results, 0)) == 0);
    assert(rt_seq_len(rt_seq_get(results, 1)) == 0);

    if (rt_obj_release_check0(results))
        rt_obj_free(results);
}

static void test_async_all_value_retain_overflow_completes_with_error() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    void *value = make_obj();
    void *futures = rt_seq_new();

    rt_promise_set_owned(promise, value);
    rt_seq_push(futures, future);

    rt_heap_hdr_t *hdr = rt_heap_hdr(value);
    hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    void *all_future = rt_async_all(futures);
    assert(all_future != NULL);
    assert(rt_future_wait_for(all_future, 250) == 1);
    assert(rt_future_is_error(all_future) == 1);

    rt_string err = rt_future_get_error(all_future);
    assert(std::strstr(rt_string_cstr(err), "refcount overflow") != NULL);
    rt_string_unref(err);

    hdr->refcnt = 2;
    if (rt_obj_release_check0(value))
        rt_obj_free(value);
    if (rt_obj_release_check0(all_future))
        rt_obj_free(all_future);
    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);
    if (rt_obj_release_check0(futures))
        rt_obj_free(futures);
}

static void test_async_all_handles_already_complete_inputs() {
    void *p1 = rt_promise_new();
    void *f1 = rt_promise_get_future(p1);
    void *p2 = rt_promise_new();
    void *f2 = rt_promise_get_future(p2);
    void *v1 = make_obj();
    void *v2 = make_obj();
    void *futures = rt_seq_new();

    rt_promise_set_owned(p1, v1);
    rt_promise_set_owned(p2, v2);
    rt_seq_push(futures, f1);
    rt_seq_push(futures, f2);

    void *all_future = rt_async_all(futures);
    assert(all_future != NULL);
    assert(rt_future_wait_for(all_future, 1) == 1);

    void *results = rt_future_get(all_future);
    assert(rt_seq_len(results) == 2);
    assert(rt_seq_get(results, 0) == v1);
    assert(rt_seq_get(results, 1) == v2);
}

static void test_async_all_already_complete_error_short_circuits() {
    void *error_promise = rt_promise_new();
    void *error_future = rt_promise_get_future(error_promise);
    void *pending_promise = rt_promise_new();
    void *pending_future = rt_promise_get_future(pending_promise);
    void *futures = rt_seq_new();

    rt_promise_set_error(error_promise, rt_const_cstr("early boom"));
    rt_seq_push(futures, error_future);
    rt_seq_push(futures, pending_future);

    void *all_future = rt_async_all(futures);
    assert(all_future != NULL);
    assert(rt_future_wait_for(all_future, 1) == 1);
    assert(rt_future_is_error(all_future) == 1);
    assert(strcmp(rt_string_cstr(rt_future_get_error(all_future)), "early boom") == 0);

    rt_promise_set_owned(pending_promise, make_obj());
}

//=============================================================================
// rt_async_any tests
//=============================================================================

static void test_async_any_basic() {
    void *futures = rt_seq_new();
    void *fast_val = make_obj();
    void *slow_val1 = make_obj();
    void *slow_val2 = make_obj();

    // One fast, two slow
    void *fast_future = rt_async_run((void *)identity_cb, fast_val);
    void *slow_future1 = rt_async_run((void *)slow_cb, slow_val1);
    void *slow_future2 = rt_async_run((void *)slow_cb, slow_val2);
    rt_seq_push(futures, fast_future);
    rt_seq_push(futures, slow_future1);
    rt_seq_push(futures, slow_future2);

    void *any_future = rt_async_any(futures);
    assert(any_future != NULL);

    void *result = rt_future_get(any_future);
    // The fast one should complete first
    assert(result == fast_val);

    // Async.Any cancels remaining listeners, not the underlying work. Wait for
    // the intentionally slow workers so process-exit finalizer sweep cannot race
    // detached async completions.
    rt_future_wait(slow_future1);
    rt_future_wait(slow_future2);
}

static void test_async_any_empty() {
    void *futures = rt_seq_new();
    void *any_future = rt_async_any(futures);
    assert(any_future != NULL);

    // Should resolve with error
    rt_future_wait(any_future);
    assert(rt_future_is_error(any_future) == 1);
}

static void test_async_any_rejects_huge_count() {
    void *futures = rt_seq_new();
    corrupt_seq_len(futures, INT64_MAX);

    void *any_future = rt_async_any(futures);
    assert(any_future != NULL);
    assert(rt_future_is_done(any_future) == 1);
    assert(rt_future_is_error(any_future) == 1);

    rt_string err = rt_future_get_error(any_future);
    assert(std::strstr(rt_string_cstr(err), "Async.Any: futures count too large") != NULL);
    rt_string_unref(err);
}

static void test_async_any_handles_already_complete_winner() {
    void *winner_promise = rt_promise_new();
    void *winner_future = rt_promise_get_future(winner_promise);
    void *pending_promise = rt_promise_new();
    void *pending_future = rt_promise_get_future(pending_promise);
    void *winner = make_obj();
    void *futures = rt_seq_new();

    rt_promise_set_owned(winner_promise, winner);
    rt_seq_push(futures, winner_future);
    rt_seq_push(futures, pending_future);

    void *any_future = rt_async_any(futures);
    assert(any_future != NULL);
    assert(rt_future_wait_for(any_future, 1) == 1);
    assert(rt_future_get(any_future) == winner);

    rt_promise_set_owned(pending_promise, make_obj());
    assert(rt_future_get(any_future) == winner);
}

static void test_async_any_value_retain_overflow_completes_with_error() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    void *value = make_obj();
    void *futures = rt_seq_new();

    rt_promise_set_owned(promise, value);
    rt_seq_push(futures, future);

    rt_heap_hdr_t *hdr = rt_heap_hdr(value);
    hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    void *any_future = rt_async_any(futures);
    assert(any_future != NULL);
    assert(rt_future_wait_for(any_future, 250) == 1);
    assert(rt_future_is_error(any_future) == 1);

    rt_string err = rt_future_get_error(any_future);
    assert(std::strstr(rt_string_cstr(err), "refcount overflow") != NULL);
    rt_string_unref(err);

    hdr->refcnt = 2;
    if (rt_obj_release_check0(value))
        rt_obj_free(value);
    if (rt_obj_release_check0(any_future))
        rt_obj_free(any_future);
    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);
    if (rt_obj_release_check0(futures))
        rt_obj_free(futures);
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

static void test_async_map_retains_callback_result() {
    void *source = rt_async_run((void *)new_obj_cb, NULL);
    void *mapped = rt_async_map(source, (void *)add_one_mapper, NULL);
    assert(mapped != NULL);

    rt_future_wait(mapped);
    assert(rt_future_value_is_owned(mapped) == 1);
    void *result = rt_future_get(mapped);
    assert(result != NULL);

    if (rt_obj_release_check0(mapped))
        rt_obj_free(mapped);
    if (rt_obj_release_check0(source))
        rt_obj_free(source);
    if (rt_obj_release_check0(result))
        rt_obj_free(result);
}

static void test_async_map_retains_borrowed_mapper_result() {
    void *source_value = make_obj();
    void *source = rt_async_run((void *)identity_cb, source_value);
    void *borrowed = rt_seq_new();
    g_borrowed_map_result = borrowed;

    void *mapped = rt_async_map(source, (void *)borrowed_result_mapper, NULL);
    assert(mapped != NULL);
    void *result = rt_future_get(mapped);
    assert(result == borrowed);

    if (rt_obj_release_check0(borrowed))
        rt_obj_free(borrowed);
    g_borrowed_map_result = nullptr;
    if (rt_obj_release_check0(mapped))
        rt_obj_free(mapped);

    assert(rt_seq_len(result) == 0);
    if (rt_obj_release_check0(result))
        rt_obj_free(result);
    if (rt_obj_release_check0(source))
        rt_obj_free(source);
    if (rt_obj_release_check0(source_value))
        rt_obj_free(source_value);
}

static void test_async_map_releases_peeked_source_when_mapper_traps() {
    g_map_source_finalized.store(0, std::memory_order_release);

    void *promise = rt_promise_new();
    void *source = rt_promise_get_future(promise);
    void *source_value = make_obj();
    rt_obj_set_finalizer(source_value, map_source_finalizer);

    rt_promise_set_owned(promise, source_value);
    if (rt_obj_release_check0(source_value))
        rt_obj_free(source_value);

    void *mapped = rt_async_map(source, (void *)trapping_mapper, NULL);
    assert(mapped != NULL);
    rt_future_wait(mapped);
    assert(rt_future_is_error(mapped) == 1);
    assert(strcmp(rt_string_cstr(rt_future_get_error(mapped)), "async map trap") == 0);

    if (rt_obj_release_check0(mapped))
        rt_obj_free(mapped);
    if (rt_obj_release_check0(source))
        rt_obj_free(source);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);

    assert(g_map_source_finalized.load(std::memory_order_acquire) == 1);
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

static void test_cancellable_pre_cancelled() {
    g_precancel_calls.store(0);
    void *token = rt_cancellation_new();
    rt_cancellation_cancel(token);

    void *future = rt_async_run_cancellable((void *)precancel_counting_cb, NULL, token);
    assert(future != NULL);
    rt_future_wait(future);

    assert(rt_future_is_error(future) == 1);
    assert(strcmp(rt_string_cstr(rt_future_get_error(future)), "cancelled") == 0);
    assert(g_precancel_calls.load() == 0);
}

static void test_async_run_trap_becomes_error() {
    g_trap_calls.store(0);
    void *future = rt_async_run((void *)trap_cb, NULL);
    assert(future != NULL);

    rt_future_wait(future);
    assert(rt_future_is_error(future) == 1);
    assert(strcmp(rt_string_cstr(rt_future_get_error(future)), "async trap") == 0);
    assert(g_trap_calls.load() == 1);
}

//=============================================================================
// Timing tests
//=============================================================================

static void test_async_runs_concurrently() {
    const int N = 5;
    void *futures[N];
    void *values[N];

    g_concurrent_started.store(0, std::memory_order_release);
    g_concurrent_active.store(0, std::memory_order_release);
    g_concurrent_peak.store(0, std::memory_order_release);
    g_concurrent_release.store(0, std::memory_order_release);

    for (int i = 0; i < N; i++) {
        values[i] = make_obj();
        futures[i] = rt_async_run((void *)barrier_concurrent_cb, values[i]);
        assert(futures[i] != NULL);
    }

    bool all_started_before_release = wait_for_started_count(N, 5000);
    int peak_before_release = g_concurrent_peak.load(std::memory_order_acquire);
    g_concurrent_release.store(1, std::memory_order_release);

    for (int i = 0; i < N; i++) {
        void *result = rt_future_get(futures[i]);
        assert(result == values[i]);
    }

    assert(all_started_before_release);
    assert(peak_before_release == N);
}

/// @brief Main.
int main() {
    test_async_run_basic();
    test_async_run_null_arg();
    test_async_run_multiple();
    test_async_run_owned_keeps_object_arg_alive();
    test_async_run_owned_passthrough_preserves_result();
    test_async_run_retains_callback_result();
    test_async_run_retains_borrowed_callback_result();
    test_async_delay();
    test_async_delay_zero();
    test_async_delay_negative();
    test_async_all_basic();
    test_async_all_empty();
    test_async_all_null();
    test_async_all_rejects_huge_count();
    test_async_all_short_circuits_on_error();
    test_async_all_result_owns_values();
    test_async_all_value_retain_overflow_completes_with_error();
    test_async_all_handles_already_complete_inputs();
    test_async_all_already_complete_error_short_circuits();
    test_async_any_basic();
    test_async_any_empty();
    test_async_any_rejects_huge_count();
    test_async_any_handles_already_complete_winner();
    test_async_any_value_retain_overflow_completes_with_error();
    test_async_map_basic();
    test_async_map_chained();
    test_async_map_retains_callback_result();
    test_async_map_retains_borrowed_mapper_result();
    test_async_map_releases_peeked_source_when_mapper_traps();
    test_cancellable_normal();
    test_cancellable_cancelled();
    test_cancellable_null_token();
    test_cancellable_pre_cancelled();
    test_async_run_trap_becomes_error();
    test_async_runs_concurrently();

    // Futures are resolved before the detached runtime thread wrapper has
    // necessarily finished releasing its thread handle/context. Give those
    // cleanup tails a bounded drain before the process-exit finalizer sweep.
    rt_thread_sleep(100);

    printf("Async tests: all passed\n");
    return 0;
}
