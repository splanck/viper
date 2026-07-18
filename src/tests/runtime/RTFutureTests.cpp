//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTFutureTests.cpp
// Purpose: Tests for Zanna.Threads.Future/Promise — async value delivery,
//   blocking get, timeout, cancellation, and multi-producer scenarios.
// Key invariants: Settlement occurs once, listener ordering is stable, cached
//                 Futures promote safely, and all managed cycles are visible.
// Ownership/Lifetime: Tests join producer/listener threads before releasing
//                     Promise, Future, continuation, value, and error objects.
// Links: src/runtime/threads/rt_future.c, docs/zannalib/threads.md,
//        docs/adr/0133-runtime-concurrency-and-collection-hardening.md
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_future.h"
#include "rt_gc.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <setjmp.h>
#include <thread>
#include <vector>

static bool g_returning_trap_enabled = false;
static int g_returning_trap_count = 0;
static char g_returning_trap_message[256];
static int g_future_alloc_fail_countdown = 0;

/// @brief Fail one selected runtime allocation in Promise OOM regressions.
/// @details The hook decrements @ref g_future_alloc_fail_countdown and rejects
///          the call that reaches zero. Every other allocation delegates to the
///          allocator supplied by the runtime, so the test can isolate error-
///          message construction without replacing normal heap behavior.
/// @param bytes Requested allocation size.
/// @param next Default runtime allocator.
/// @return Allocated storage, or NULL for the selected injected failure.
static void *future_fail_countdown_alloc(int64_t bytes, void *(*next)(int64_t)) {
    if (g_future_alloc_fail_countdown > 0 && --g_future_alloc_fail_countdown == 0)
        return nullptr;
    return next(bytes);
}

/// @brief Record a trap and return when the returning-hook probe is enabled.
/// @details Normal test execution remains fail-fast through @ref rt_abort. The
///          enabled mode models an embedder hook that records a failure and
///          resumes, as explicitly permitted by the runtime trap contract.
/// @param msg Null-terminated trap message supplied by the runtime.
extern "C" void vm_trap(const char *msg) {
    if (!g_returning_trap_enabled)
        rt_abort(msg);
    ++g_returning_trap_count;
    std::snprintf(g_returning_trap_message, sizeof(g_returning_trap_message), "%s", msg ? msg : "");
}

/// @brief Begin one returning-trap probe with cleared observation state.
static void begin_returning_trap_probe() {
    g_returning_trap_count = 0;
    g_returning_trap_message[0] = '\0';
    g_returning_trap_enabled = true;
}

/// @brief End a returning-trap probe and restore fail-fast test behavior.
static void end_returning_trap_probe() {
    g_returning_trap_enabled = false;
}

static void test_result(bool cond, const char *name) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", name);
        assert(false);
    }
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
    test_result(rt_gc_is_tracked(promise) == 1, "get_future: Promise is cycle tracked");
    test_result(rt_gc_is_tracked(future) == 1, "get_future: Future is cycle tracked");
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

/// @brief Verify native worker rejection completes even when its diagnostic cannot allocate.
/// @details The first managed allocation used to copy the C-string diagnostic
///          is failed deterministically. The no-throw producer API must still
///          publish a completed error state with an empty optional message,
///          wake the Future, and reject a later duplicate without trapping.
static void test_promise_try_set_error_cstr_oom_fallback() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    g_future_alloc_fail_countdown = 1;
    rt_set_alloc_hook(future_fail_countdown_alloc);
    int8_t completed = rt_promise_try_set_error_cstr(promise, "worker failed");
    rt_set_alloc_hook(nullptr);

    test_result(completed == 1, "try_set_error_cstr: OOM still completes promise");
    test_result(g_future_alloc_fail_countdown == 0,
                "try_set_error_cstr: injected diagnostic allocation failed");
    test_result(rt_future_wait_for(future, 0) == 1,
                "try_set_error_cstr: future is immediately settled");
    test_result(rt_future_is_error(future) == 1,
                "try_set_error_cstr: fallback preserves error state");
    rt_string error = rt_future_get_error(future);
    test_result(error != nullptr && rt_str_len(error) == 0,
                "try_set_error_cstr: OOM fallback has optional empty message");
    rt_string_unref(error);
    test_result(rt_promise_try_set_error_cstr(promise, "duplicate") == 0,
                "try_set_error_cstr: duplicate completion returns false");

    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);
}

/// @brief Verify best-effort transferred completion consumes every supplied value.
/// @details The first managed Box becomes the successful Future result. A second
///          Box supplied after settlement and a third Box supplied with an
///          invalid Promise are both consumed without invoking the trap
///          dispatcher, proving worker cleanup cannot leak duplicate results.
static void test_promise_try_set_transferred_consumes_all_paths() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    void *first = rt_box_i64(41);

    test_result(rt_promise_try_set_transferred(promise, first) == 1,
                "try_set_transferred: first producer completes promise");
    void *received = rt_future_get(future);
    test_result(received != nullptr && rt_unbox_i64(received) == 41,
                "try_set_transferred: future receives transferred box");
    if (rt_obj_release_check0(received))
        rt_obj_free(received);

    void *duplicate = rt_box_i64(42);
    test_result(rt_promise_try_set_transferred(promise, duplicate) == 0,
                "try_set_transferred: duplicate producer returns false");
    test_result(!rt_heap_is_payload(duplicate),
                "try_set_transferred: duplicate value reference is consumed");

    void *invalid_value = rt_box_i64(43);
    test_result(rt_promise_try_set_transferred(nullptr, invalid_value) == 0,
                "try_set_transferred: invalid promise returns false");
    test_result(!rt_heap_is_payload(invalid_value),
                "try_set_transferred: invalid-promise value reference is consumed");

    if (rt_obj_release_check0(future))
        rt_obj_free(future);
    if (rt_obj_release_check0(promise))
        rt_obj_free(promise);
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
    test_result(out == NULL, "try_get_empty: should clear out on pending future");
    void *option = rt_future_try_get_option(future);
    test_result(rt_option_is_none(option) == 1, "try_get_option_empty: should return None");
}

static void test_future_try_get_error_clears_out() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    rt_promise_set_error(promise, rt_const_cstr("try_get error"));

    void *out = (void *)0xDEAD;
    int8_t result = rt_future_try_get(future, &out);

    test_result(result == 0, "try_get_error: should return 0 on error");
    test_result(out == NULL, "try_get_error: should clear out on error");
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
    void *option = rt_future_try_get_option(future);
    test_result(rt_option_is_some(option) == 1 && rt_option_unwrap(option) == &value,
                "try_get_option_value: should return Some(value)");
}

static void test_future_try_get_option_null_value() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    rt_promise_set(promise, NULL);

    void *option = rt_future_try_get_option(future);
    test_result(rt_option_is_some(option) == 1, "try_get_option_null: should return Some");
    test_result(rt_option_unwrap(option) == NULL, "try_get_option_null: payload should be NULL");
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

static void test_future_huge_timeout_resolved() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int value = 790;
    rt_promise_set(promise, &value);

    test_result(rt_future_wait_for(future, INT64_MAX) == 1,
                "huge_timeout_resolved: wait_for should accept saturated timeout");

    void *out = NULL;
    test_result(rt_future_get_for(future, INT64_MAX, &out) == 1,
                "huge_timeout_resolved: get_for should accept saturated timeout");
    test_result(out == &value, "huge_timeout_resolved: get_for should return value");
    test_result(rt_future_get_for_val(future, INT64_MAX) == &value,
                "huge_timeout_resolved: get_for_val should return value");
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
    test_result(out == NULL, "get_for_timeout: should clear out on timeout");
}

static void test_future_get_for_error_clears_out() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    rt_promise_set_error(promise, rt_const_cstr("get_for error"));

    void *out = (void *)0xDEAD;
    int8_t result = rt_future_get_for(future, 1000, &out);

    test_result(result == 0, "get_for_error: should return 0 on error");
    test_result(out == NULL, "get_for_error: should clear out on error");
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

static void release_runtime_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int g_rejected_transferred_finalizer_count = 0;

static void rejected_transferred_finalizer(void *obj) {
    (void)obj;
    ++g_rejected_transferred_finalizer_count;
}

static void test_transferred_value_released_when_promise_already_done() {
    g_rejected_transferred_finalizer_count = 0;
    void *promise = rt_promise_new();
    rt_promise_set(promise, nullptr);

    void *rejected = rt_obj_new_i64(0x7F001234, 1);
    rt_obj_set_finalizer(rejected, rejected_transferred_finalizer);

    test_result(expect_trap([&]() { rt_promise_set_transferred(promise, rejected); }),
                "transferred_rejected: completed promise should trap");
    test_result(g_rejected_transferred_finalizer_count == 1,
                "transferred_rejected: rejected transferred value should be released");

    release_runtime_object(promise);
}

/// @brief Verify completed Promise setters stop after a returning trap hook.
/// @details Every setter must report exactly one "already completed" trap,
///          preserve the first completion, release temporary ownership, and
///          leave the Promise mutex usable after the embedder hook returns.
static void test_completed_promise_returning_traps_preserve_first_result() {
    int first = 41;
    int second = 42;

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    rt_promise_set(promise, &first);

    begin_returning_trap_probe();
    rt_promise_set(promise, &second);
    end_returning_trap_probe();
    test_result(g_returning_trap_count == 1, "returning_trap_set: reports one completed trap");
    test_result(std::strstr(g_returning_trap_message, "already completed") != nullptr,
                "returning_trap_set: reports completed diagnostic");
    test_result(rt_future_get(future) == &first, "returning_trap_set: preserves first completion");

    void *owned = rt_seq_new();
    begin_returning_trap_probe();
    rt_promise_set_owned(promise, owned);
    end_returning_trap_probe();
    test_result(g_returning_trap_count == 1,
                "returning_trap_set_owned: reports one completed trap");
    test_result(rt_seq_len(owned) == 0, "returning_trap_set_owned: caller ownership remains valid");

    g_rejected_transferred_finalizer_count = 0;
    void *transferred = rt_obj_new_i64(0x7F001235, 1);
    rt_obj_set_finalizer(transferred, rejected_transferred_finalizer);
    begin_returning_trap_probe();
    rt_promise_set_transferred(promise, transferred);
    end_returning_trap_probe();
    test_result(g_returning_trap_count == 1,
                "returning_trap_set_transferred: reports one completed trap");
    test_result(g_rejected_transferred_finalizer_count == 1,
                "returning_trap_set_transferred: releases transferred value");

    begin_returning_trap_probe();
    rt_promise_set_error(promise, rt_const_cstr("ignored error"));
    end_returning_trap_probe();
    test_result(g_returning_trap_count == 1,
                "returning_trap_set_error: reports one completed trap");
    test_result(rt_future_get(future) == &first,
                "returning_trap_set_error: preserves first completion");

    release_runtime_object(owned);
    release_runtime_object(future);
    release_runtime_object(promise);
}

static void test_promise_set_retain_overflow_does_not_lock_promise() {
    void *promise = rt_promise_new();
    void *value = rt_seq_new();
    rt_heap_hdr_t *value_hdr = rt_heap_hdr(value);
    value_hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    test_result(expect_trap([&]() { rt_promise_set(promise, value); }),
                "promise_set_overflow: should trap");
    test_result(!rt_promise_is_done(promise), "promise_set_overflow: promise remains usable");

    value_hdr->refcnt = 1;
    int fallback = 1;
    rt_promise_set(promise, &fallback);
    test_result(rt_promise_is_done(promise), "promise_set_overflow: promise can complete later");

    release_runtime_object(value);
    release_runtime_object(promise);
}

static void test_promise_set_owned_retain_overflow_does_not_lock_promise() {
    void *promise = rt_promise_new();
    void *value = rt_seq_new();
    rt_heap_hdr_t *value_hdr = rt_heap_hdr(value);
    value_hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    test_result(expect_trap([&]() { rt_promise_set_owned(promise, value); }),
                "promise_set_owned_overflow: should trap");
    test_result(!rt_promise_is_done(promise), "promise_set_owned_overflow: promise remains usable");

    value_hdr->refcnt = 1;
    int fallback = 2;
    rt_promise_set(promise, &fallback);
    test_result(rt_promise_is_done(promise),
                "promise_set_owned_overflow: promise can complete later");

    release_runtime_object(value);
    release_runtime_object(promise);
}

static void test_get_future_cached_retain_overflow_does_not_lock_promise() {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    rt_heap_hdr_t *future_hdr = rt_heap_hdr(future);
    future_hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    test_result(expect_trap([&]() { (void)rt_promise_get_future(promise); }),
                "get_future_cached_overflow: should trap");

    future_hdr->refcnt = 1;
    void *future2 = rt_promise_get_future(promise);
    test_result(future2 == future, "get_future_cached_overflow: cached future remains usable");

    release_runtime_object(future2);
    release_runtime_object(future);
    release_runtime_object(promise);
}

struct PoisonedFuture {
    void *promise;
    void *future;
    void *value;
    rt_heap_hdr_t *value_hdr;
};

static PoisonedFuture make_poisoned_owned_future() {
    PoisonedFuture pf{};
    pf.promise = rt_promise_new();
    pf.future = rt_promise_get_future(pf.promise);
    pf.value = rt_seq_new();
    rt_promise_set_transferred(pf.promise, pf.value);
    pf.value_hdr = rt_heap_hdr(pf.value);
    pf.value_hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;
    return pf;
}

static void cleanup_poisoned_owned_future(PoisonedFuture &pf) {
    pf.value_hdr->refcnt = 1;
    release_runtime_object(pf.future);
    release_runtime_object(pf.promise);
    pf = {};
}

static void test_future_get_retain_overflow_does_not_lock_future() {
    PoisonedFuture pf = make_poisoned_owned_future();

    test_result(expect_trap([&]() { (void)rt_future_get(pf.future); }),
                "future_get_overflow: should trap");
    test_result(rt_future_is_done(pf.future), "future_get_overflow: future remains usable");

    cleanup_poisoned_owned_future(pf);
}

static void test_future_get_for_retain_overflow_does_not_lock_future() {
    PoisonedFuture pf = make_poisoned_owned_future();
    void *out = (void *)0x1;

    test_result(expect_trap([&]() { (void)rt_future_get_for(pf.future, 1000, &out); }),
                "future_get_for_overflow: should trap");
    test_result(rt_future_is_done(pf.future), "future_get_for_overflow: future remains usable");

    cleanup_poisoned_owned_future(pf);
}

static void test_future_try_get_retain_overflow_does_not_lock_future() {
    PoisonedFuture pf = make_poisoned_owned_future();
    void *out = (void *)0x1;

    test_result(expect_trap([&]() { (void)rt_future_try_get(pf.future, &out); }),
                "future_try_get_overflow: should trap");
    test_result(rt_future_is_done(pf.future), "future_try_get_overflow: future remains usable");

    cleanup_poisoned_owned_future(pf);
}

static void test_future_try_get_val_retain_overflow_does_not_lock_future() {
    PoisonedFuture pf = make_poisoned_owned_future();

    test_result(expect_trap([&]() { (void)rt_future_try_get_val(pf.future); }),
                "future_try_get_val_overflow: should trap");
    test_result(rt_future_is_done(pf.future), "future_try_get_val_overflow: future remains usable");

    cleanup_poisoned_owned_future(pf);
}

static void test_future_get_for_val_retain_overflow_does_not_lock_future() {
    PoisonedFuture pf = make_poisoned_owned_future();

    test_result(expect_trap([&]() { (void)rt_future_get_for_val(pf.future, 1000); }),
                "future_get_for_val_overflow: should trap");
    test_result(rt_future_is_done(pf.future), "future_get_for_val_overflow: future remains usable");

    cleanup_poisoned_owned_future(pf);
}

static void test_future_peek_value_retain_overflow_does_not_lock_future() {
    PoisonedFuture pf = make_poisoned_owned_future();

    test_result(expect_trap([&]() { (void)rt_future_peek_value(pf.future); }),
                "future_peek_value_overflow: should trap");
    test_result(rt_future_is_done(pf.future), "future_peek_value_overflow: future remains usable");

    cleanup_poisoned_owned_future(pf);
}

static int g_listener_trap_count = 0;
static int g_listener_ok_count = 0;
static int g_cancel_hook_count = 0;
static int g_cancel_listener_count = 0;

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

static void passive_cancel_listener(void *future, void *ctx) {
    (void)future;
    (void)ctx;
    ++g_cancel_listener_count;
}

static void trapping_cancel_hook(void *ctx) {
    (void)ctx;
    ++g_cancel_hook_count;
    rt_trap("cancel hook trap");
}

static void test_listener_trap_isolated_after_cleanup() {
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

    test_result(trapped == 0, "listener_trap: promise set should isolate listener trap");
    test_result(g_listener_trap_count == 1, "listener_trap: trapping listener called");
    test_result(g_listener_ok_count == 1, "listener_trap: later listener still called");
}

static void test_completed_future_listener_trap_isolated() {
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

    test_result(trapped == 0, "completed_listener_trap: should isolate listener trap");
    test_result(g_listener_trap_count == 1, "completed_listener_trap: listener called once");
}

static void test_cancel_listener_trap_isolated_after_cleanup() {
    g_cancel_hook_count = 0;
    g_cancel_listener_count = 0;

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    int value = 3;

    test_result(rt_future_on_complete_ex(
                    future, passive_cancel_listener, nullptr, trapping_cancel_hook) == 1,
                "cancel_listener_trap: register listener");

    test_result(rt_future_cancel_listener(future, passive_cancel_listener, nullptr) == 1,
                "cancel_listener_trap: should remove listener");
    test_result(g_cancel_hook_count == 1, "cancel_listener_trap: cancel hook called once");

    rt_promise_set(promise, &value);
    test_result(g_cancel_listener_count == 0,
                "cancel_listener_trap: removed listener should not complete");
}

struct OrderedListenerContext {
    int expected;
};

static int g_ordered_listener_next = 0;

/// @brief Assert FIFO listener delivery for the O(1) Promise tail queue.
static void ordered_listener(void *future, void *ctx) {
    (void)future;
    auto *ordered = static_cast<OrderedListenerContext *>(ctx);
    assert(ordered != nullptr);
    assert(g_ordered_listener_next == ordered->expected);
    ++g_ordered_listener_next;
}

/// @brief Verify listener append remains FIFO after removing the current tail.
static void test_listener_fifo_tail_after_cancel() {
    constexpr int kListenerCount = 1024;
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    std::vector<OrderedListenerContext> contexts(kListenerCount);

    for (int i = 0; i < kListenerCount; ++i) {
        contexts[i].expected = i;
        test_result(rt_future_on_complete(future, ordered_listener, &contexts[i]) == 1,
                    "listener_fifo: append succeeds");
    }
    test_result(
        rt_future_cancel_listener(future, ordered_listener, &contexts[kListenerCount - 1]) == 1,
        "listener_fifo: remove tail succeeds");

    OrderedListenerContext replacement{kListenerCount - 1};
    test_result(rt_future_on_complete(future, ordered_listener, &replacement) == 1,
                "listener_fifo: append after tail removal succeeds");

    g_ordered_listener_next = 0;
    int value = 9;
    rt_promise_set(promise, &value);
    test_result(g_ordered_listener_next == kListenerCount,
                "listener_fifo: every listener runs in registration order");
    release_runtime_object(future);
    release_runtime_object(promise);
}

/// @brief Stress atomic promotion of the Promise's weak cached Future pointer.
/// @details Each caller repeatedly obtains and releases the same cached wrapper.
///          A final release may race another lookup before the finalizer clears
///          the raw cache, which must create a replacement instead of reviving
///          a zero-reference object or trapping.
static void test_cached_future_concurrent_weak_promotion() {
    constexpr int kThreads = 6;
    constexpr int kIterations = 2000;
    void *promise = rt_promise_new();
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int thread_index = 0; thread_index < kThreads; ++thread_index) {
        workers.emplace_back([promise]() {
            for (int i = 0; i < kIterations; ++i) {
                void *future = rt_promise_get_future(promise);
                assert(future != nullptr);
                if ((i & 15) == 0)
                    std::this_thread::yield();
                release_runtime_object(future);
            }
        });
    }
    for (auto &worker : workers)
        worker.join();

    void *future = rt_promise_get_future(promise);
    int value = 10;
    rt_promise_set(promise, &value);
    test_result(rt_future_get(future) == &value,
                "future_cached_promotion: surviving wrapper resolves normally");
    release_runtime_object(future);
    release_runtime_object(promise);
}

static int g_abandoned_listener_calls = 0;
static int g_abandoned_cancel_calls = 0;

static void abandoned_listener(void *future, void *ctx) {
    (void)future;
    (void)ctx;
    ++g_abandoned_listener_calls;
}

static void abandoned_cancel(void *ctx) {
    (void)ctx;
    ++g_abandoned_cancel_calls;
}

/// @brief Verify the pending listener Promise/Future cycle is reclaimable.
/// @details The listener owns the Future and the Future owns the Promise. After
///          external references are dropped, cycle collection must cancel the
///          abandoned continuation, never report it as completed, and reclaim
///          both wrappers.
static void test_pending_listener_cycle_is_collected() {
    g_abandoned_listener_calls = 0;
    g_abandoned_cancel_calls = 0;
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    test_result(rt_future_on_complete_ex(future, abandoned_listener, nullptr, abandoned_cancel) ==
                    1,
                "listener_cycle: registration succeeds");

    void *promise_address = promise;
    void *future_address = future;
    release_runtime_object(future);
    release_runtime_object(promise);

    test_result(rt_gc_collect() >= 2, "listener_cycle: collector reclaims cycle members");
    test_result(!rt_heap_is_payload(promise_address), "listener_cycle: Promise reclaimed");
    test_result(!rt_heap_is_payload(future_address), "listener_cycle: Future reclaimed");
    test_result(g_abandoned_listener_calls == 0,
                "listener_cycle: abandoned callback is not reported as completion");
    test_result(g_abandoned_cancel_calls == 1,
                "listener_cycle: abandoned continuation is cancelled exactly once");
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
    test_promise_try_set_error_cstr_oom_fallback();
    test_promise_try_set_transferred_consumes_all_paths();

    // Basic future tests
    test_future_is_done_false();
    test_future_try_get_empty();
    test_future_try_get_error_clears_out();
    test_future_try_get_value();
    test_future_try_get_option_null_value();
    test_future_get_immediate();

    // Wait tests
    test_future_wait_for_timeout();
    test_future_wait_for_resolved();
    test_future_huge_timeout_resolved();

    // Threading tests
    test_async_resolution();
    test_async_error();

    // Edge cases
    test_null_safety();
    test_future_get_for_timeout();
    test_future_get_for_error_clears_out();
    test_future_get_for_success();
    test_future_recreate_after_release();
    test_cached_future_retained_for_each_caller();
    test_owned_value_survives_future_release();
    test_owned_try_get_survives_future_release();
    test_owned_get_for_val_survives_future_release();
    test_transferred_value_survives_future_release();
    test_set_value_survives_producer_release();
    test_transferred_value_released_when_promise_already_done();
    test_completed_promise_returning_traps_preserve_first_result();
    test_promise_set_retain_overflow_does_not_lock_promise();
    test_promise_set_owned_retain_overflow_does_not_lock_promise();
    test_get_future_cached_retain_overflow_does_not_lock_promise();
    test_future_get_retain_overflow_does_not_lock_future();
    test_future_get_for_retain_overflow_does_not_lock_future();
    test_future_try_get_retain_overflow_does_not_lock_future();
    test_future_try_get_val_retain_overflow_does_not_lock_future();
    test_future_get_for_val_retain_overflow_does_not_lock_future();
    test_future_peek_value_retain_overflow_does_not_lock_future();
    test_listener_trap_isolated_after_cleanup();
    test_completed_future_listener_trap_isolated();
    test_cancel_listener_trap_isolated_after_cleanup();
    test_listener_fifo_tail_after_cancel();
    test_cached_future_concurrent_weak_promotion();
    test_pending_listener_cycle_is_collected();

    printf("All Future/Promise tests passed!\n");
    return 0;
}
