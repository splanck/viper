//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Purpose: Tests for Viper.Threads.Future/Promise — async value delivery,
//   blocking get, timeout, cancellation, and multi-producer scenarios.
//
//===----------------------------------------------------------------------===//

#include "rt_future.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <setjmp.h>
#include <thread>

extern "C" void rt_trap(const char *msg);
extern "C" void rt_trap_set_recovery(jmp_buf *buf);
extern "C" void rt_trap_clear_recovery(void);

static void test_result(bool cond, const char *name) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", name);
        assert(false);
    }
}

//=============================================================================
// Basic Promise Tests
//=============================================================================

static void test_promise_new() {
    void *promise = rt_promise_new();
    test_result(promise != NULL, "promise_new: should create promise");
    test_result(!rt_promise_is_done(promise), "promise_new: should not be done initially");
}

static void test_promise_get_future() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    test_result(future != NULL, "get_future: should return future");

    // Multiple calls return the same future
    void *future2 = rt_promise_get_future(promise);
    test_result(future == future2, "get_future: should return same future");
}

static void test_promise_set() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int value = 42;
    rt_promise_set(promise, &value);

    test_result(rt_promise_is_done(promise), "promise_set: promise should be done");
    test_result(rt_future_is_done(future), "promise_set: future should be done");
    test_result(!rt_future_is_error(future), "promise_set: should not be error");
}

static void test_promise_set_error() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    rt_promise_set_error(promise, rt_const_cstr("Test error message"));

    test_result(rt_promise_is_done(promise), "promise_set_error: promise should be done");
    test_result(rt_future_is_done(future), "promise_set_error: future should be done");
    test_result(rt_future_is_error(future), "promise_set_error: should be error");

    rt_string error = rt_future_get_error(future);
    test_result(strcmp(rt_string_cstr(error), "Test error message") == 0,
                "promise_set_error: should have correct error message");
}

//=============================================================================
// Basic Future Tests
//=============================================================================

static void test_future_is_done_false() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    test_result(!rt_future_is_done(future), "future_is_done: should be false initially");
}

static void test_future_try_get_empty() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    void *out = (void *)0xDEAD;
    int8_t result = rt_future_try_get(future, &out);

    test_result(result == 0, "try_get_empty: should return 0 when not done");
}

static void test_future_try_get_value() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int value = 123;
    rt_promise_set(promise, &value);

    void *out = NULL;
    int8_t result = rt_future_try_get(future, &out);

    test_result(result == 1, "try_get_value: should return 1 when done");
    test_result(out == &value, "try_get_value: should return correct value");
}

static void test_future_get_immediate() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int value = 456;
    rt_promise_set(promise, &value);

    void *out = rt_future_get(future);
    test_result(out == &value, "get_immediate: should return correct value");
}

//=============================================================================
// Wait Tests
//=============================================================================

static void test_future_wait_for_timeout() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    auto start = std::chrono::steady_clock::now();
    int8_t result = rt_future_wait_for(future, 50); // 50ms timeout
    auto end = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    test_result(result == 0, "wait_for_timeout: should return 0 on timeout");
    test_result(elapsed >= 40, "wait_for_timeout: should wait approximately 50ms");
}

static void test_future_wait_for_resolved() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int value = 789;
    rt_promise_set(promise, &value);

    int8_t result = rt_future_wait_for(future, 1000); // Should return immediately
    test_result(result == 1, "wait_for_resolved: should return 1 when already resolved");
}

//=============================================================================
// Threading Tests
//=============================================================================

static void test_async_resolution() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int value = 999;

    // Start a thread that will resolve the promise after a delay
    std::thread resolver([promise, &value]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        rt_promise_set(promise, &value);
    });

    // Wait for the future
    int8_t result = rt_future_wait_for(future, 5000);
    test_result(result == 1, "async_resolution: should resolve");

    void *out = rt_future_get(future);
    test_result(out == &value, "async_resolution: should have correct value");

    resolver.join();
}

static void test_async_error() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    // Start a thread that will fail the promise
    std::thread resolver([promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        rt_promise_set_error(promise, rt_const_cstr("Async error"));
    });

    int8_t result = rt_future_wait_for(future, 5000);
    test_result(result == 1, "async_error: should resolve");
    test_result(rt_future_is_error(future), "async_error: should be error");

    rt_string error = rt_future_get_error(future);
    test_result(strcmp(rt_string_cstr(error), "Async error") == 0,
                "async_error: should have correct error message");

    resolver.join();
}

//=============================================================================
// Edge Cases
//=============================================================================

static void test_null_safety() {
    // These should not crash
    test_result(!rt_promise_is_done(NULL), "null_safety: promise_is_done on NULL");
    test_result(!rt_future_is_done(NULL), "null_safety: future_is_done on NULL");
    test_result(!rt_future_is_error(NULL), "null_safety: future_is_error on NULL");
    test_result(strlen(rt_string_cstr(rt_future_get_error(NULL))) == 0,
                "null_safety: future_get_error on NULL returns empty");
    test_result(!rt_future_try_get(NULL, NULL), "null_safety: future_try_get on NULL");
    test_result(!rt_future_wait_for(NULL, 10), "null_safety: future_wait_for on NULL");
}

static void test_future_get_for_timeout() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    void *out = NULL;
    int8_t result = rt_future_get_for(future, 30, &out);

    test_result(result == 0, "get_for_timeout: should return 0 on timeout");
}

static void test_future_get_for_success() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int value = 1234;
    rt_promise_set(promise, &value);

    void *out = NULL;
    int8_t result = rt_future_get_for(future, 1000, &out);

    test_result(result == 1, "get_for_success: should return 1");
    test_result(out == &value, "get_for_success: should return correct value");
}

static void test_future_recreate_after_release() {
    void *promise = rt_promise_new();
    void *future1 = rt_promise_get_future(promise);
    test_result(future1 != nullptr, "future_recreate: initial future created");

    if (rt_obj_release_check0(future1))
        rt_obj_free(future1);

    void *future2 = rt_promise_get_future(promise);
    test_result(future2 != nullptr, "future_recreate: future recreated after release");

    int value = 2026;
    rt_promise_set(promise, &value);
    test_result(rt_future_get(future2) == &value, "future_recreate: recreated future resolves");

    if (rt_obj_release_check0(future2))
        rt_obj_free(future2);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);
}

static void test_cached_future_retained_for_each_caller() {
    void *promise = rt_promise_new();
    void *future1 = rt_promise_get_future(promise);
    void *future2 = rt_promise_get_future(promise);
    test_result(future1 == future2, "future_cached_retain: should return cached future");

    if (rt_obj_release_check0(future1))
        rt_obj_free(future1);

    int value = 2027;
    rt_promise_set(promise, &value);
    test_result(rt_future_get(future2) == &value,
                "future_cached_retain: second caller should remain valid");

    if (rt_obj_release_check0(future2))
        rt_obj_free(future2);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);
}

static void test_owned_value_survives_future_release() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    void *seq = rt_seq_new();

    rt_promise_set_owned(promise, seq);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);

    void *got = rt_future_get(future);
    test_result(got != nullptr, "owned_value: future get returns seq");
    test_result(rt_seq_len(got) == 0, "owned_value: returned seq usable before future release");

    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);

    test_result(rt_seq_len(got) == 0, "owned_value: returned seq survives future/promise release");
    if (rt_obj_release_check0(got))
        rt_obj_free(got);
}

static void test_owned_try_get_survives_future_release() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    void *seq = rt_seq_new();

    rt_promise_set_owned(promise, seq);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);

    void *got = NULL;
    test_result(rt_future_try_get(future, &got) == 1, "owned_try_get: should resolve");
    test_result(got != nullptr, "owned_try_get: should return seq");

    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);

    test_result(rt_seq_len(got) == 0,
                "owned_try_get: returned seq survives future/promise release");
    if (rt_obj_release_check0(got))
        rt_obj_free(got);
}

static void test_owned_get_for_val_survives_future_release() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    void *seq = rt_seq_new();

    rt_promise_set_owned(promise, seq);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);

    void *got = rt_future_get_for_val(future, 1000);
    test_result(got != nullptr, "owned_get_for_val: should return seq");

    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);

    test_result(rt_seq_len(got) == 0,
                "owned_get_for_val: returned seq survives future/promise release");
    if (rt_obj_release_check0(got))
        rt_obj_free(got);
}

static void test_transferred_value_survives_future_release() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    void *seq = rt_seq_new();

    rt_promise_set_transferred(promise, seq);

    void *got = rt_future_get(future);
    test_result(got != nullptr, "transferred_value: future get returns seq");

    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);

    test_result(rt_seq_len(got) == 0,
                "transferred_value: returned seq survives future/promise release");
    if (rt_obj_release_check0(got))
        rt_obj_free(got);
}

static void test_set_value_survives_producer_release() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    void *seq = rt_seq_new();

    rt_promise_set(promise, seq);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);

    void *got = rt_future_get(future);
    test_result(got != nullptr, "set_value: future get returns seq");

    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);

    test_result(rt_seq_len(got) == 0, "set_value: returned seq survives future/promise release");
    if (rt_obj_release_check0(got))
        rt_obj_free(got);
}

static int g_listener_trap_count = 0;
static int g_listener_ok_count = 0;

static void trapping_listener(void *future, void *ctx) {
    (void)future;
    (void)ctx;
    ++g_listener_trap_count;
    rt_trap("listener trap");
}

static void ok_listener(void *future, void *ctx) {
    (void)future;
    (void)ctx;
    ++g_listener_ok_count;
}

static void test_listener_trap_rethrows_after_cleanup() {
    g_listener_trap_count = 0;
    g_listener_ok_count = 0;

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    int value = 1;

    test_result(rt_future_on_complete(future, trapping_listener, nullptr) == 1,
                "listener_trap: register trapping listener");
    test_result(rt_future_on_complete(future, ok_listener, nullptr) == 1,
                "listener_trap: register ok listener");

    jmp_buf recovery;
    int trapped = 0;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        rt_promise_set(promise, &value);
    } else {
        trapped = 1;
    }
    rt_trap_clear_recovery();

    test_result(trapped == 1, "listener_trap: promise set should rethrow listener trap");
    test_result(g_listener_trap_count == 1, "listener_trap: trapping listener called");
    test_result(g_listener_ok_count == 1, "listener_trap: later listener still called");
}

static void test_completed_future_listener_trap_cleans_up() {
    g_listener_trap_count = 0;

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    int value = 2;
    rt_promise_set(promise, &value);

    jmp_buf recovery;
    int trapped = 0;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        (void)rt_future_on_complete(future, trapping_listener, nullptr);
    } else {
        trapped = 1;
    }
    rt_trap_clear_recovery();

    test_result(trapped == 1, "completed_listener_trap: should rethrow listener trap");
    test_result(g_listener_trap_count == 1, "completed_listener_trap: listener called once");
}

//=============================================================================
// Main
//=============================================================================

int main() {
    // Basic promise tests
    test_promise_new();
    test_promise_get_future();
    test_promise_set();
    test_promise_set_error();

    // Basic future tests
    test_future_is_done_false();
    test_future_try_get_empty();
    test_future_try_get_value();
    test_future_get_immediate();

    // Wait tests
    test_future_wait_for_timeout();
    test_future_wait_for_resolved();

    // Threading tests
    test_async_resolution();
    test_async_error();

    // Edge cases
    test_null_safety();
    test_future_get_for_timeout();
    test_future_get_for_success();
    test_future_recreate_after_release();
    test_cached_future_retained_for_each_caller();
    test_owned_value_survives_future_release();
    test_owned_try_get_survives_future_release();
    test_owned_get_for_val_survives_future_release();
    test_transferred_value_survives_future_release();
    test_set_value_survives_producer_release();
    test_listener_trap_rethrows_after_cleanup();
    test_completed_future_listener_trap_cleans_up();

    printf("All Future/Promise tests passed!\n");
    return 0;
}
