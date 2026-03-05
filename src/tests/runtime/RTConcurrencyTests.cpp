//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTConcurrencyTests.cpp
// Purpose: Tests for concurrency fixes CONC-001 through CONC-010.
//          Exercises init races, TOCTOU, volatile removal, and spinlock
//          backoff under multi-threaded stress.
// Key invariants:
//   - GC, string intern, pool, and context subsystems must be safe under
//     concurrent first-use from multiple threads.
//   - GC auto-trigger must not double-collect (at most one thread claims
//     the counter reset via CAS).
//   - Pool alloc/free must be consistent after multi-threaded stress.
// Ownership/Lifetime:
//   - Each test function is self-contained; global state is reset between tests.
// Links: hardening/concurrency.md
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

extern "C"
{
#include "rt_context.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pool.h"
#include "rt_stack_safety.h"
#include "rt_string.h"
#include "rt_string_intern.h"

    void vm_trap(const char *msg)
    {
        fprintf(stderr, "TRAP: %s\n", msg);
        rt_abort(msg);
    }
}

//=============================================================================
// CONC-001: GC lock init race — concurrent first use from multiple threads
//=============================================================================

static void test_gc_concurrent_first_use()
{
    printf("  test_gc_concurrent_first_use...");

    // Shut down GC to reset lock state, then hammer it from multiple threads.
    rt_gc_shutdown();

    constexpr int kThreads = 8;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++)
    {
        threads.emplace_back(
            [&go]()
            {
                while (!go.load(std::memory_order_acquire))
                {
                    // spin until all threads are ready
                }
                // All threads call GC functions simultaneously — first call
                // must safely initialize the lock without corruption.
                rt_gc_tracked_count();
                rt_gc_is_tracked(nullptr);
                int64_t n = rt_gc_collect();
                (void)n;
            });
    }

    // Release all threads at once
    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    // Verify GC is in a consistent state
    assert(rt_gc_tracked_count() == 0);
    printf(" PASS\n");
}

//=============================================================================
// CONC-002: String intern lock init race — concurrent first intern
//=============================================================================

static void test_string_intern_concurrent_first_use()
{
    printf("  test_string_intern_concurrent_first_use...");

    // Drain to reset state
    rt_string_intern_drain();

    constexpr int kThreads = 8;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    std::vector<rt_string> results(kThreads, nullptr);

    for (int i = 0; i < kThreads; i++)
    {
        threads.emplace_back(
            [&go, &results, i]()
            {
                while (!go.load(std::memory_order_acquire))
                {
                    // spin
                }
                // All threads intern the same string simultaneously
                rt_string s = rt_string_from_bytes("concurrent_test", 15);
                results[i] = rt_string_intern(s);
                rt_string_unref(s);
            });
    }

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    // All results should be the same canonical pointer
    for (int i = 1; i < kThreads; i++)
    {
        assert(results[i] == results[0] && "All interned strings must share the same pointer");
    }

    // Cleanup
    for (int i = 0; i < kThreads; i++)
    {
        if (results[i])
            rt_string_unref(results[i]);
    }

    rt_string_intern_drain();
    printf(" PASS\n");
}

//=============================================================================
// CONC-003: GC auto-trigger TOCTOU — no double-collect under contention
//=============================================================================

static std::atomic<int64_t> g_collect_count{0};

// We need to count how many times rt_gc_collect is actually triggered.
// Since we can't easily intercept it, we use rt_gc_pass_count() before and after.

static void test_gc_notify_alloc_no_double_collect()
{
    printf("  test_gc_notify_alloc_no_double_collect...");

    // Set a low threshold so the counter triggers frequently
    rt_gc_set_threshold(10);

    int64_t pass_before = rt_gc_pass_count();

    constexpr int kThreads = 8;
    constexpr int kAllocsPerThread = 100;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++)
    {
        threads.emplace_back(
            [&go]()
            {
                while (!go.load(std::memory_order_acquire))
                {
                    // spin
                }
                for (int j = 0; j < kAllocsPerThread; j++)
                {
                    rt_gc_notify_alloc();
                }
            });
    }

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    int64_t pass_after = rt_gc_pass_count();
    int64_t total_allocs = kThreads * kAllocsPerThread;
    int64_t collections = pass_after - pass_before;

    // With threshold=10 and 800 allocs, we expect ~80 collections.
    // With the CAS fix, each threshold crossing triggers exactly one collect.
    // Without the fix, double-collects would inflate this count.
    // Allow some slack since thread scheduling is non-deterministic.
    int64_t expected_max = total_allocs / 10 + kThreads; // upper bound with thread slack

    assert(collections > 0 && "Should have triggered at least one collection");
    assert(collections <= expected_max && "Too many collections — possible double-collect regression");

    rt_gc_set_threshold(0);
    printf(" PASS (collections=%lld, expected_max=%lld)\n",
           (long long)collections, (long long)expected_max);
}

//=============================================================================
// CONC-005: Pool alloc/free — volatile removal correctness
//=============================================================================

static void test_pool_concurrent_alloc_free()
{
    printf("  test_pool_concurrent_alloc_free...");

    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 200;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++)
    {
        threads.emplace_back(
            [&go]()
            {
                while (!go.load(std::memory_order_acquire))
                {
                    // spin
                }
                // Allocate and free pool blocks from multiple threads
                void *ptrs[kOpsPerThread];
                for (int j = 0; j < kOpsPerThread; j++)
                {
                    ptrs[j] = rt_pool_alloc(64);
                    assert(ptrs[j] != nullptr);
                    // Write to the block to verify it's valid memory
                    memset(ptrs[j], 0xAB, 64);
                }
                for (int j = 0; j < kOpsPerThread; j++)
                {
                    rt_pool_free(ptrs[j], 64);
                }
            });
    }

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    // Verify stats are consistent
    size_t allocated = 0, free_count = 0;
    rt_pool_stats(RT_POOL_64, &allocated, &free_count);
    assert(allocated == 0 && "All blocks should be freed");
    assert(free_count > 0 && "Freed blocks should be on the freelist");

    printf(" PASS (free_count=%zu)\n", free_count);
}

//=============================================================================
// CONC-007: Stack safety init — concurrent double-init is safe
//=============================================================================

static void test_stack_safety_concurrent_init()
{
    printf("  test_stack_safety_concurrent_init...");

    constexpr int kThreads = 8;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++)
    {
        threads.emplace_back(
            [&go]()
            {
                while (!go.load(std::memory_order_acquire))
                {
                    // spin
                }
                // Multiple threads call init simultaneously — should not crash
                rt_init_stack_safety();
            });
    }

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    printf(" PASS\n");
}

//=============================================================================
// CONC-010: Context spinlock with yield — concurrent context binding
//=============================================================================

static void test_context_concurrent_bind_unbind()
{
    printf("  test_context_concurrent_bind_unbind...");

    // Each thread creates its own context, binds it, does work, unbinds.
    // This exercises the spinlock in rt_set_current_context.
    constexpr int kThreads = 8;
    constexpr int kIterations = 50;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    // Ensure legacy context is initialized first
    (void)rt_legacy_context();

    for (int i = 0; i < kThreads; i++)
    {
        threads.emplace_back(
            [&go]()
            {
                while (!go.load(std::memory_order_acquire))
                {
                    // spin
                }
                for (int j = 0; j < kIterations; j++)
                {
                    RtContext ctx;
                    rt_context_init(&ctx);
                    rt_set_current_context(&ctx);
                    assert(rt_get_current_context() == &ctx);
                    rt_set_current_context(nullptr);
                    rt_context_cleanup(&ctx);
                }
            });
    }

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    printf(" PASS\n");
}

//=============================================================================
// CONC-001 + CONC-003: GC shutdown/reinit cycle — lock survives reset
//=============================================================================

static void test_gc_shutdown_reinit_cycle()
{
    printf("  test_gc_shutdown_reinit_cycle...");

    // Shut down and reinitialize the GC multiple times to verify the lock
    // reset logic works correctly (CONC-001: INIT_ONCE / PTHREAD_MUTEX_INITIALIZER).
    for (int cycle = 0; cycle < 5; cycle++)
    {
        rt_gc_set_threshold(100);
        for (int i = 0; i < 200; i++)
        {
            rt_gc_notify_alloc();
        }
        assert(rt_gc_pass_count() > 0);
        rt_gc_shutdown();
    }

    // Verify it works after the last shutdown
    rt_gc_set_threshold(5);
    for (int i = 0; i < 10; i++)
    {
        rt_gc_notify_alloc();
    }
    assert(rt_gc_pass_count() > 0);
    rt_gc_set_threshold(0);

    printf(" PASS\n");
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("RTConcurrencyTests:\n");

    test_gc_concurrent_first_use();
    test_string_intern_concurrent_first_use();
    test_gc_notify_alloc_no_double_collect();
    test_pool_concurrent_alloc_free();
    test_stack_safety_concurrent_init();
    test_context_concurrent_bind_unbind();
    test_gc_shutdown_reinit_cycle();

    printf("All concurrency tests passed.\n");
    return 0;
}
