//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "rt_platform.h"

#if RT_PLATFORM_MACOS || RT_PLATFORM_LINUX
#include <signal.h>
#endif

extern "C" {
#include "rt_args.h"
#include "rt_context.h"
#include "rt_file.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_modvar.h"
#include "rt_object.h"
#include "rt_pool.h"
#include "rt_random.h"
#include "rt_stack_safety.h"
#include "rt_string.h"
#include "rt_string_intern.h"

void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}
}

//=============================================================================
// CONC-001: GC lock init race — concurrent first use from multiple threads
//=============================================================================

static void test_gc_concurrent_first_use() {
    printf("  test_gc_concurrent_first_use...");

    // Shut down GC to reset lock state, then hammer it from multiple threads.
    rt_gc_shutdown();

    constexpr int kThreads = 8;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++) {
        threads.emplace_back([&go]() {
            while (!go.load(std::memory_order_acquire)) {
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

static void test_string_intern_concurrent_first_use() {
    printf("  test_string_intern_concurrent_first_use...");

    // Drain to reset state
    rt_string_intern_drain();

    constexpr int kThreads = 8;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    std::vector<rt_string> results(kThreads, nullptr);

    for (int i = 0; i < kThreads; i++) {
        threads.emplace_back([&go, &results, i]() {
            while (!go.load(std::memory_order_acquire)) {
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
    for (int i = 1; i < kThreads; i++) {
        assert(results[i] == results[0] && "All interned strings must share the same pointer");
    }

    // Cleanup
    for (int i = 0; i < kThreads; i++) {
        if (results[i])
            rt_string_unref(results[i]);
    }

    rt_string_intern_drain();
    printf(" PASS\n");
}

//=============================================================================
// CONC-003: GC allocation debt coalesces under contention
//=============================================================================

static std::atomic<int64_t> g_collect_count{0};

// We need to count how many times rt_gc_collect is actually triggered.
// Since we can't easily intercept it, we use rt_gc_pass_count() before and after.

static void test_gc_notify_alloc_no_double_collect() {
    printf("  test_gc_notify_alloc_no_double_collect...");

    // Set a low threshold so the counter triggers frequently
    rt_gc_set_threshold(10);

    int64_t pass_before = rt_gc_pass_count();

    constexpr int kThreads = 8;
    constexpr int kAllocsPerThread = 100;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++) {
        threads.emplace_back([&go]() {
            while (!go.load(std::memory_order_acquire)) {
                // spin
            }
            for (int j = 0; j < kAllocsPerThread; j++) {
                rt_gc_notify_alloc();
            }
        });
    }

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

    assert(rt_gc_pass_count() == pass_before &&
           "Allocation notification must not collect inside allocator context");
    rt_gc_safepoint();
    int64_t collections = rt_gc_pass_count() - pass_before;
    assert(collections == 1 && "Concurrent threshold crossings should coalesce into one pass");

    rt_gc_set_threshold(0);
    printf(" PASS (coalesced_collections=%lld)\n", (long long)collections);
}

//=============================================================================
// CONC-005: Pool alloc/free — volatile removal correctness
//=============================================================================

static void test_pool_concurrent_alloc_free() {
    printf("  test_pool_concurrent_alloc_free...");

    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 200;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreads; i++) {
        threads.emplace_back([&go]() {
            while (!go.load(std::memory_order_acquire)) {
                // spin
            }
            // Allocate and free pool blocks from multiple threads
            void *ptrs[kOpsPerThread];
            for (int j = 0; j < kOpsPerThread; j++) {
                ptrs[j] = rt_pool_alloc(64);
                assert(ptrs[j] != nullptr);
                // Write to the block to verify it's valid memory
                memset(ptrs[j], 0xAB, 64);
            }
            for (int j = 0; j < kOpsPerThread; j++) {
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

/// @brief Verify process-wide handler publication and per-thread stack setup.
/// @details All workers initialize concurrently and remain alive until every
///          POSIX alternate-stack address has been captured. Distinct enabled
///          addresses prove that no two threads share the fallback buffer. A
///          later main-thread call proves handler readiness does not bypass the
///          caller-specific setup. Under ASan this also verifies that the
///          sanitizer-owned alternate stack is preserved through thread exit.
static void test_stack_safety_concurrent_init() {
    printf("  test_stack_safety_concurrent_init...");

    constexpr int kThreads = 8;
    std::atomic<bool> go{false};
    std::atomic<int> initialized{0};
    std::vector<std::thread> threads;
#if RT_PLATFORM_MACOS || RT_PLATFORM_LINUX
    std::vector<void *> alt_stacks(kThreads, nullptr);
#endif

    for (int i = 0; i < kThreads; i++) {
        threads.emplace_back([&, i]() {
            while (!go.load(std::memory_order_acquire)) {
                // spin
            }
            rt_init_stack_safety();
#if RT_PLATFORM_MACOS || RT_PLATFORM_LINUX
            stack_t current{};
            assert(sigaltstack(nullptr, &current) == 0);
            assert((current.ss_flags & SS_DISABLE) == 0);
            alt_stacks[i] = current.ss_sp;
#endif
            initialized.fetch_add(1, std::memory_order_release);
            while (initialized.load(std::memory_order_acquire) != kThreads)
                std::this_thread::yield();
        });
    }

    go.store(true, std::memory_order_release);

    for (auto &t : threads)
        t.join();

#if RT_PLATFORM_MACOS || RT_PLATFORM_LINUX
    for (int i = 0; i < kThreads; ++i) {
        assert(alt_stacks[i] != nullptr);
        for (int j = 0; j < i; ++j)
            assert(alt_stacks[i] != alt_stacks[j]);
    }
    rt_init_stack_safety();
    stack_t main_stack{};
    assert(sigaltstack(nullptr, &main_stack) == 0);
    assert((main_stack.ss_flags & SS_DISABLE) == 0);
#endif

    printf(" PASS\n");
}

//=============================================================================
// CONC-010: Context spinlock with yield — concurrent context binding
//=============================================================================

static void test_context_concurrent_bind_unbind() {
    printf("  test_context_concurrent_bind_unbind...");

    // Each thread creates its own context, binds it, does work, unbinds.
    // This exercises the spinlock in rt_set_current_context.
    constexpr int kThreads = 8;
    constexpr int kIterations = 50;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    // Ensure legacy context is initialized first
    (void)rt_legacy_context();

    for (int i = 0; i < kThreads; i++) {
        threads.emplace_back([&go]() {
            while (!go.load(std::memory_order_acquire)) {
                // spin
            }
            for (int j = 0; j < kIterations; j++) {
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

/// @brief Stress every mutable state family shared by inherited context bindings.
/// @details A parent binding keeps the context live while workers concurrently
///          advance the deterministic RNG, resolve the same module-variable key,
///          append arguments, and create independent BASIC file channels. Exact
///          final RNG state detects lost updates; stable addresses/counts detect
///          table races; successful per-channel output detects file-table races.
static void test_context_shared_mutable_state_is_serialized() {
    printf("  test_context_shared_mutable_state_is_serialized...");

    constexpr int kThreads = 8;
    constexpr int kRandomSteps = 2000;
    constexpr uint64_t kSeed = UINT64_C(0x123456789ABCDEF0);
    RtContext ctx{};
    rt_context_init(&ctx);
    rt_set_current_context(&ctx);
    rt_args_clear();
    rt_randomize_u64(kSeed);

    rt_string modvar_name = rt_string_from_bytes("shared-context-counter", 22);
    std::vector<void *> addresses(kThreads, nullptr);
    std::vector<std::string> paths;
    paths.reserve(kThreads);
    const auto base = std::filesystem::temp_directory_path();
    for (int i = 0; i < kThreads; ++i) {
        paths.push_back((base / ("zanna-context-race-" + std::to_string((uintptr_t)&ctx) + "-" +
                                 std::to_string(i) + ".tmp"))
                            .string());
    }

    std::atomic<bool> go{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() {
            rt_set_current_context(&ctx);
            while (!go.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (int j = 0; j < kRandomSteps; ++j)
                (void)rt_rnd();
            addresses[i] = rt_modvar_addr_i64(modvar_name);

            std::string arg_text = "worker-" + std::to_string(i);
            rt_string arg = rt_string_from_bytes(arg_text.data(), arg_text.size());
            rt_args_push(arg);
            rt_string_unref(arg);

            rt_string path = rt_string_from_bytes(paths[i].data(), paths[i].size());
            rt_string payload = rt_string_from_bytes("ok", 2);
            int channel = i + 1;
            if (rt_open_err_vstr(path, RT_F_OUTPUT, channel) != 0 ||
                rt_write_ch_err(channel, payload) != 0 || rt_close_err(channel) != 0)
                failures.fetch_add(1, std::memory_order_relaxed);
            rt_string_unref(payload);
            rt_string_unref(path);
            rt_set_current_context(nullptr);
        });
    }

    go.store(true, std::memory_order_release);
    for (auto &thread : threads)
        thread.join();

    uint64_t expected_state = kSeed;
    for (int i = 0; i < kThreads * kRandomSteps; ++i)
        expected_state = expected_state * UINT64_C(6364136223846793005) + UINT64_C(1);
    assert(ctx.rng_state == expected_state && "Concurrent RNG steps must not be lost");
    for (int i = 0; i < kThreads; ++i)
        assert(addresses[i] == addresses[0] && "Concurrent modvar lookup must publish one slot");
    assert(ctx.modvar_count == 1 && "Concurrent modvar creation must not duplicate entries");
    assert(rt_args_count() == kThreads && "Concurrent argument appends must all remain visible");
    assert(failures.load(std::memory_order_relaxed) == 0 &&
           "Concurrent file-channel operations must remain consistent");

    for (const std::string &path : paths) {
        std::error_code ignored;
        (void)std::filesystem::remove(path, ignored);
    }
    rt_args_clear();
    rt_string_unref(modvar_name);
    rt_set_current_context(nullptr);
    rt_context_cleanup(&ctx);

    constexpr uint64_t kLegacySeed = UINT64_C(0x0F1E2D3C4B5A6978);
    rt_randomize_u64(kLegacySeed);
    threads.clear();
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([=]() {
            for (int j = 0; j < kRandomSteps; ++j)
                (void)rt_rnd();
        });
    }
    for (auto &thread : threads)
        thread.join();
    uint64_t expected_legacy_state = kLegacySeed;
    for (int i = 0; i < kThreads * kRandomSteps; ++i)
        expected_legacy_state = expected_legacy_state * UINT64_C(6364136223846793005) + UINT64_C(1);
    assert(rt_legacy_context()->rng_state == expected_legacy_state &&
           "Unbound native threads must serialize shared legacy RNG state");
    printf(" PASS\n");
}

//=============================================================================
// CONC-001 + CONC-003: GC shutdown/reinit cycle — lock survives reset
//=============================================================================

static void test_gc_shutdown_reinit_cycle() {
    printf("  test_gc_shutdown_reinit_cycle...");

    // Shut down and reinitialize the GC multiple times to verify the lock
    // reset logic works correctly (CONC-001: INIT_ONCE / PTHREAD_MUTEX_INITIALIZER).
    for (int cycle = 0; cycle < 5; cycle++) {
        rt_gc_set_threshold(100);
        for (int i = 0; i < 200; i++) {
            rt_gc_notify_alloc();
        }
        rt_gc_safepoint();
        assert(rt_gc_pass_count() > 0);
        rt_gc_shutdown();
    }

    // Verify it works after the last shutdown
    rt_gc_set_threshold(5);
    for (int i = 0; i < 10; i++) {
        rt_gc_notify_alloc();
    }
    rt_gc_safepoint();
    assert(rt_gc_pass_count() > 0);
    rt_gc_set_threshold(0);

    printf(" PASS\n");
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("RTConcurrencyTests:\n");

    test_gc_concurrent_first_use();
    test_string_intern_concurrent_first_use();
    test_gc_notify_alloc_no_double_collect();
    test_pool_concurrent_alloc_free();
    test_stack_safety_concurrent_init();
    test_context_concurrent_bind_unbind();
    test_context_shared_mutable_state_is_serialized();
    test_gc_shutdown_reinit_cycle();

    printf("All concurrency tests passed.\n");
    return 0;
}
