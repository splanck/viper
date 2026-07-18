//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTThreadsThreadTests.cpp
// Purpose: Validate Zanna.Threads.Thread and SafeI64 runtime primitives.
// Key invariants: Thread join/timeout semantics work; SafeI64 operations are thread-safe.
// Ownership/Lifetime: Uses runtime library and OS threads; skip on Windows.
// Links: src/runtime/threads/rt_threads_posix.c,
//        src/runtime/threads/rt_threads_win.c,
//        docs/adr/0136-runtime-context-binding-lifecycle-and-state-locks.md
//
//===----------------------------------------------------------------------===//

#include "rt_context.h"
#include "rt_heap.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_threads.h"

#include "common/ProcessIsolation.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static void call_thread_start_null() {
    (void)rt_thread_start(nullptr, nullptr);
}

static void call_thread_join_null() {
    rt_thread_join(nullptr);
}

extern "C" void quick_entry(void *arg) {
    (void)arg;
}

/// @brief Release a joined thread handle and verify eventual object reclamation.
/// @details Join publishes that the entry callback has completed, but a POSIX
///          worker may still be dropping its one internal lifecycle reference
///          after waking joiners. Therefore the caller-visible release can
///          report either zero references or that one transient reference. In
///          the latter schedule, this helper yields until the worker removes the
///          payload from the managed-heap registry, retaining the leak check
///          without requiring an ordering that the Thread contract does not
///          promise. The stale address is used only as a registry key and is
///          never dereferenced after release.
/// @param thread Joined regular or safe Thread handle owned by the caller.
static void release_joined_thread_and_assert_reclaimed(void *thread) {
    const int64_t remaining = rt_memory_release(thread);
    assert(remaining == 0 || remaining == 1);
    if (remaining == 0)
        return;

    constexpr int kReclamationYields = 100000;
    for (int attempt = 0; attempt < kReclamationYields && rt_heap_is_payload(thread); ++attempt)
        rt_thread_yield();
    assert(rt_heap_is_payload(thread) == 0);
}

static void call_thread_start_owned_retain_overflow() {
    void *arg = rt_obj_new_i64(0, sizeof(int64_t));
    rt_heap_hdr(arg)->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;
    (void)rt_thread_start_owned((void *)&quick_entry, arg);
}

static void call_thread_start_safe_owned_retain_overflow() {
    void *arg = rt_obj_new_i64(0, sizeof(int64_t));
    rt_heap_hdr(arg)->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;
    (void)rt_thread_start_safe_owned((void *)&quick_entry, arg);
}

static void call_thread_join_twice() {
    void *t = rt_thread_start((void *)&quick_entry, nullptr);
    assert(t != nullptr);
    rt_thread_join(t);
    rt_thread_join(t);
    assert(rt_thread_try_join(t) == 1);
    assert(rt_thread_join_for(t, 0) == 1);
}

static void call_thread_try_join_after_join() {
    void *t = rt_thread_start((void *)&quick_entry, nullptr);
    assert(t != nullptr);
    rt_thread_join(t);
    assert(rt_thread_try_join(t) == 1);
}

static void call_thread_join_fake_magic_wrong_class() {
    uint32_t *fake = static_cast<uint32_t *>(rt_obj_new_i64(0, sizeof(uint32_t)));
    *fake = 0x56545244u;
    rt_thread_join(fake);
}

extern "C" void add_loop_entry(void *arg) {
    void *cell = arg;
    for (int i = 0; i < 1000; ++i)
        (void)rt_safe_i64_add(cell, 1);
}

static void test_safe_i64_concurrent_add() {
    void *cell = rt_safe_i64_new(0);
    assert(cell != nullptr);

    constexpr int kThreads = 4;
    std::vector<void *> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
        threads.push_back(rt_thread_start((void *)&add_loop_entry, cell));

    for (void *t : threads)
        rt_thread_join(t);

    int64_t final = rt_safe_i64_get(cell);
    assert(final == 1000LL * kThreads);
}

/// @brief Exercise typed callback starts and the fast-exit detach handshake.
/// @details A worker may finish before its creator returns from the native
///          spawn call. Repeating that schedule verifies that the creator and
///          worker coordinate one detach owner while all handles remain
///          joinable through the runtime completion condition.
static void test_typed_thread_start_fast_exit_stress() {
    constexpr int kIterations = 512;
    for (int i = 0; i < kIterations; ++i) {
        void *thread = rt_thread_start_fn(quick_entry, nullptr);
        assert(thread != nullptr);
        rt_thread_join(thread);
        release_joined_thread_and_assert_reclaimed(thread);
    }

    void *safe_thread = rt_thread_start_safe_fn(quick_entry, nullptr);
    assert(safe_thread != nullptr);
    rt_thread_safe_join(safe_thread);
    assert(rt_thread_has_error(safe_thread) == 0);
    release_joined_thread_and_assert_reclaimed(safe_thread);
}

static void test_safe_i64_add_wraps() {
    void *cell = rt_safe_i64_new(INT64_MAX);
    assert(cell != nullptr);
    assert(rt_safe_i64_add(cell, 1) == INT64_MIN);
    assert(rt_safe_i64_get(cell) == INT64_MIN);
}

extern "C" void sleep_then_store(void *arg) {
    auto *p = static_cast<std::atomic<int> *>(arg);
    rt_thread_sleep(50);
    p->store(1, std::memory_order_release);
}

extern "C" void long_sleep_then_store(void *arg) {
    auto *p = static_cast<std::atomic<int> *>(arg);
    rt_thread_sleep(100);
    p->store(1, std::memory_order_release);
}

static std::atomic<int64_t> g_owned_thread_arg_len{0};
static std::atomic<int> g_safe_thread_flag{0};

typedef struct InheritedContextProbe {
    RtContext *expected;
    std::atomic<int> release;
    std::atomic<int> saw_expected;
} InheritedContextProbe;

extern "C" void observe_owned_seq(void *arg) {
    rt_thread_sleep(20);
    g_owned_thread_arg_len.store(rt_seq_len(arg), std::memory_order_release);
}

extern "C" void set_safe_thread_flag(void *arg) {
    (void)arg;
    rt_thread_sleep(20);
    g_safe_thread_flag.store(1, std::memory_order_release);
}

/// @brief Hold a spawned thread until its parent verifies the context reservation.
/// @details Records whether the child trampoline installed the expected
///          context, then waits so the parent can inspect the binding count
///          before and after releasing its own binding.
/// @param arg Pointer to an InheritedContextProbe owned by the joining parent.
extern "C" void probe_inherited_context(void *arg) {
    auto *probe = static_cast<InheritedContextProbe *>(arg);
    probe->saw_expected.store(rt_get_current_context() == probe->expected,
                              std::memory_order_release);
    while (probe->release.load(std::memory_order_acquire) == 0)
        rt_thread_yield();
}

/// @brief Verify Thread.Start reserves its inherited context before returning.
/// @details The parent and child initially account for two bindings. The
///          parent's unbind leaves the child's reservation live until the
///          blocked entry exits, proving context storage cannot become
///          cleanup-eligible in the native-create-to-trampoline window.
static void test_thread_start_reserves_inherited_context() {
    RtContext context{};
    rt_context_init(&context);
    rt_set_current_context(&context);

    InheritedContextProbe probe{&context, 0, 0};
    void *thread = rt_thread_start((void *)&probe_inherited_context, &probe);
    assert(thread != nullptr);
    assert(context.bind_count == 2);

    rt_set_current_context(nullptr);
    assert(context.bind_count == 1);
    probe.release.store(1, std::memory_order_release);
    rt_thread_join(thread);

    assert(probe.saw_expected.load(std::memory_order_acquire) == 1);
    assert(context.bind_count == 0);
    rt_context_cleanup(&context);
    release_joined_thread_and_assert_reclaimed(thread);
}

static void test_thread_join_for_timeout() {
    std::atomic<int> flag{0};
    void *t = rt_thread_start((void *)&sleep_then_store, &flag);
    assert(t != nullptr);

    int8_t done = rt_thread_join_for(t, /*ms=*/1);
    assert(done == 0);

    rt_thread_join(t);
    assert(flag.load(std::memory_order_acquire) == 1);
}

static void test_thread_join_for_timeout_releases_temporary_reference() {
    std::atomic<int> flag{0};
    void *t = rt_thread_start((void *)&long_sleep_then_store, &flag);
    assert(t != nullptr);

    assert(rt_thread_join_for(t, /*ms=*/1) == 0);
    assert(rt_memory_release(t) == 1);

    for (int waited = 0; waited < 500 && flag.load(std::memory_order_acquire) == 0; ++waited)
        rt_thread_sleep(1);
    assert(flag.load(std::memory_order_acquire) == 1);
}

static void test_thread_start_owned_keeps_object_arg_alive() {
    g_owned_thread_arg_len.store(-1, std::memory_order_release);
    void *arg = rt_seq_new();
    void *t = rt_thread_start_owned((void *)&observe_owned_seq, arg);
    assert(t != nullptr);

    if (rt_obj_release_check0(arg))
        rt_obj_free(arg);

    rt_thread_join(t);
    assert(g_owned_thread_arg_len.load(std::memory_order_acquire) == 0);
}

static void test_thread_start_safe_owned_keeps_object_arg_alive() {
    g_owned_thread_arg_len.store(-1, std::memory_order_release);
    void *arg = rt_seq_new();
    void *t = rt_thread_start_safe_owned((void *)&observe_owned_seq, arg);
    assert(t != nullptr);

    if (rt_obj_release_check0(arg))
        rt_obj_free(arg);

    rt_thread_safe_join(t);
    assert(rt_thread_has_error(t) == 0);
    assert(g_owned_thread_arg_len.load(std::memory_order_acquire) == 0);
}

static void test_safe_thread_supports_standard_thread_methods() {
    g_safe_thread_flag.store(0, std::memory_order_release);
    void *t = rt_thread_start_safe((void *)&set_safe_thread_flag, nullptr);
    assert(t != nullptr);

    assert(rt_thread_get_id(t) > 0);
    assert(rt_thread_join_for(t, 1000) == 1);
    assert(rt_thread_get_is_alive(t) == 0);
    assert(rt_thread_has_error(t) == 0);
    assert(g_safe_thread_flag.load(std::memory_order_acquire) == 1);
}

static void test_safe_thread_methods_accept_regular_thread_handle() {
    g_safe_thread_flag.store(0, std::memory_order_release);
    void *t = rt_thread_start((void *)&set_safe_thread_flag, nullptr);
    assert(t != nullptr);

    assert(rt_thread_safe_get_id(t) > 0);
    rt_thread_safe_join(t);
    assert(rt_thread_safe_is_alive(t) == 0);
    assert(rt_thread_has_error(t) == 0);
    assert(strcmp(rt_string_cstr(rt_thread_get_error(t)), "") == 0);
    assert(g_safe_thread_flag.load(std::memory_order_acquire) == 1);
}

int main(int argc, char *argv[]) {
    zanna::tests::registerChildFunction(call_thread_start_null);
    zanna::tests::registerChildFunction(call_thread_join_null);
    zanna::tests::registerChildFunction(call_thread_join_fake_magic_wrong_class);
    zanna::tests::registerChildFunction(call_thread_start_owned_retain_overflow);
    zanna::tests::registerChildFunction(call_thread_start_safe_owned_retain_overflow);
    if (zanna::tests::dispatchChild(argc, argv))
        return 0;

    // Trap messages should be stable.
    auto result = zanna::tests::runIsolated(call_thread_start_null);
    assert(result.stderrText.find("Thread.Start: null entry") != std::string::npos);

    result = zanna::tests::runIsolated(call_thread_join_null);
    assert(result.stderrText.find("Thread.Join: null thread") != std::string::npos);

    call_thread_join_twice();
    call_thread_try_join_after_join();

    result = zanna::tests::runIsolated(call_thread_join_fake_magic_wrong_class);
    assert(result.stderrText.find("Thread: invalid thread handle") != std::string::npos);

    result = zanna::tests::runIsolated(call_thread_start_owned_retain_overflow);
    assert(result.stderrText.find("refcount overflow") != std::string::npos);

    result = zanna::tests::runIsolated(call_thread_start_safe_owned_retain_overflow);
    assert(result.stderrText.find("refcount overflow") != std::string::npos);

    test_thread_join_for_timeout();
    test_thread_join_for_timeout_releases_temporary_reference();
    test_thread_start_reserves_inherited_context();
    test_thread_start_owned_keeps_object_arg_alive();
    test_thread_start_safe_owned_keeps_object_arg_alive();
    test_safe_thread_supports_standard_thread_methods();
    test_safe_thread_methods_accept_regular_thread_handle();
    test_typed_thread_start_fast_exit_stress();
    test_safe_i64_concurrent_add();
    test_safe_i64_add_wraps();
    return 0;
}
