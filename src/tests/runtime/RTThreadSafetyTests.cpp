//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTThreadSafetyTests.cpp
// Purpose: Verify thread-safety properties of global mutable state:
//   - TLS parser errors are independent across threads
//   - Main thread detection works correctly
//   - String interning is thread-safe under concurrent access
//   - Atomic violation mode is visible cross-thread
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_string_intern.h"
#include "text/rt_xml.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Test 1: TLS parser errors are independent across threads
// ============================================================================

static void test_tls_parser_errors_independent(void)
{
    // Thread 1 parses valid XML (clears error), thread 2 parses empty XML (sets error).
    // After both finish, thread 2's error must not leak into thread 1's view (TLS).
    bool thread1_no_error = false;
    bool thread2_has_error = false;

    // Use a barrier-like flag to make both threads parse concurrently.
    volatile int go = 0;

    std::thread t1(
        [&]()
        {
            while (!go)
            {
            } // wait for go signal
            // Parse valid XML — should succeed with no error
            const char *xml = "<root><child/></root>";
            rt_string s = rt_string_from_bytes(xml, strlen(xml));
            void *doc = rt_xml_parse(s);
            assert(doc != NULL && "valid XML must parse");
            if (rt_obj_release_check0(doc))
                rt_obj_free(doc);
            // Check this thread's error state is clear
            rt_string err = rt_xml_error();
            thread1_no_error = (err == NULL || rt_str_len(err) == 0);
            if (err)
                rt_string_unref(err);
            rt_string_unref(s);
        });

    std::thread t2(
        [&]()
        {
            while (!go)
            {
            } // wait for go signal
            // Parse empty XML — should fail and set error
            rt_string s = rt_string_from_bytes("", 0);
            void *doc = rt_xml_parse(s);
            assert(doc == NULL && "empty XML must fail");
            rt_string err = rt_xml_error();
            thread2_has_error = (err != NULL && rt_str_len(err) > 0);
            if (err)
                rt_string_unref(err);
            rt_string_unref(s);
        });

    go = 1; // release both threads
    t1.join();
    t2.join();

    assert(thread1_no_error && "thread 1 (valid XML) must have no error");
    assert(thread2_has_error && "thread 2 (empty XML) must have an error");

    printf("test_tls_parser_errors_independent: PASSED\n");
}

// ============================================================================
// Test 2: Main thread assertion detects non-main threads
// ============================================================================

static void test_main_thread_detection(void)
{
    // Set the main thread to the current (test) thread.
    rt_set_main_thread();

    // Verify main thread detects itself
    assert(rt_is_main_thread() && "test thread should be main");

    // Spawn a worker and verify it's NOT the main thread
    bool worker_is_main = true;
    std::thread worker([&]() { worker_is_main = rt_is_main_thread() != 0; });
    worker.join();

    assert(!worker_is_main && "worker thread must not be main");

    printf("test_main_thread_detection: PASSED\n");
}

// ============================================================================
// Test 3: String interning is thread-safe under concurrent access
// ============================================================================

static void test_string_intern_concurrent(void)
{
    rt_string_intern_drain(); // clean slate

    constexpr int kThreads = 4;
    constexpr int kStringsPerThread = 100;

    // Each thread interns the same set of strings. After all threads finish,
    // each string should resolve to one canonical pointer.
    std::vector<std::thread> threads;
    std::vector<std::vector<rt_string>> results(kThreads);

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back(
            [&, t]()
            {
                results[t].resize(kStringsPerThread);
                for (int i = 0; i < kStringsPerThread; ++i)
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "key_%d", i);
                    rt_string s = rt_string_from_bytes(buf, strlen(buf));
                    results[t][i] = rt_string_intern(s);
                    rt_string_unref(s);
                }
            });
    }

    for (auto &th : threads)
        th.join();

    // Verify all threads got the same canonical pointer for each key
    for (int i = 0; i < kStringsPerThread; ++i)
    {
        rt_string canonical = results[0][i];
        for (int t = 1; t < kThreads; ++t)
        {
            assert(results[t][i] == canonical &&
                   "all threads must get same canonical pointer for same string");
        }
    }

    // Cleanup
    for (int t = 0; t < kThreads; ++t)
        for (int i = 0; i < kStringsPerThread; ++i)
            rt_string_unref(results[t][i]);

    rt_string_intern_drain();
    printf("test_string_intern_concurrent: PASSED\n");
}

// ============================================================================
// Test 4: Atomic violation mode reads correctly from worker threads
// ============================================================================

#include "il/runtime/RuntimeSignatures.hpp"

static void test_atomic_violation_mode(void)
{
    using il::runtime::InvariantViolationMode;

    // Set to Trap from main thread
    il::runtime::setInvariantViolationMode(InvariantViolationMode::Trap);

    InvariantViolationMode worker_saw = InvariantViolationMode::Abort;
    std::thread worker([&]() { worker_saw = il::runtime::getInvariantViolationMode(); });
    worker.join();

    assert(worker_saw == InvariantViolationMode::Trap &&
           "worker must see Trap mode set by main thread");

    // Restore default
    il::runtime::setInvariantViolationMode(InvariantViolationMode::Abort);

    printf("test_atomic_violation_mode: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    test_tls_parser_errors_independent();
    test_main_thread_detection();
    test_string_intern_concurrent();
    test_atomic_violation_mode();

    printf("\nAll thread-safety tests passed.\n");
    return 0;
}
