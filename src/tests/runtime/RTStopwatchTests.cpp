//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStopwatchTests.cpp
// Purpose: Validate Viper.Diagnostics.Stopwatch runtime functions.
// Key invariants: Stopwatch accumulates time correctly, IsRunning reflects state,
//                 Start/Stop/Reset/Restart behave as documented.
// Links: docs/viperlib.md

#include "rt_object.h"
#include "rt_stopwatch.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <setjmp.h>
#include <thread>
#include <vector>

static jmp_buf g_trap_env;
static int g_expect_trap = 0;

extern "C" void vm_trap(const char *msg) {
    if (g_expect_trap)
        longjmp(g_trap_env, 1);
    fprintf(stderr, "unexpected trap: %s\n", msg ? msg : "(null)");
    abort();
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_expect_trap = 1;                                                                         \
        if (setjmp(g_trap_env) == 0) {                                                             \
            (void)(expr);                                                                          \
            g_expect_trap = 0;                                                                     \
            assert(!"expected runtime trap");                                                      \
        } else {                                                                                   \
            g_expect_trap = 0;                                                                     \
        }                                                                                          \
    } while (0)

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static constexpr int64_t kRestartUpperBoundMs = 50;
static constexpr int64_t kShortSleepUpperBoundMs = 250;
static constexpr int64_t kLongSleepUpperBoundMs = 400;

static void sleep_ms(int ms) {
    Sleep((DWORD)ms);
}
#else
#include <time.h>

#if defined(__linux__)
static constexpr int64_t kRestartUpperBoundMs = 50;
static constexpr int64_t kShortSleepUpperBoundMs = 250;
static constexpr int64_t kLongSleepUpperBoundMs = 400;
#else
static constexpr int64_t kRestartUpperBoundMs = 10;
static constexpr int64_t kShortSleepUpperBoundMs = 100;
static constexpr int64_t kLongSleepUpperBoundMs = 150;
#endif

static void sleep_ms(int ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}
#endif

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static void rt_release_obj(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

/// @brief Test creating a new stopwatch.
static void test_new() {
    printf("Testing Stopwatch.New():\n");

    void *sw = rt_stopwatch_new();
    test_result("New() returns non-null", sw != nullptr);
    test_result("New() starts stopped", rt_stopwatch_is_running(sw) == 0);
    test_result("New() has zero elapsed", rt_stopwatch_elapsed_ms(sw) == 0);
    test_result("New() has zero elapsed (ns)", rt_stopwatch_elapsed_ns(sw) == 0);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test StartNew() factory method.
static void test_start_new() {
    printf("Testing Stopwatch.StartNew():\n");

    void *sw = rt_stopwatch_start_new();
    test_result("StartNew() returns non-null", sw != nullptr);
    test_result("StartNew() creates running stopwatch", rt_stopwatch_is_running(sw) != 0);

    // Let some time pass
    sleep_ms(10);

    int64_t elapsed = rt_stopwatch_elapsed_ms(sw);
    test_result("StartNew() accumulates time", elapsed >= 5); // Allow some slack
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test Start() and Stop() behavior.
static void test_start_stop() {
    printf("Testing Start/Stop:\n");

    void *sw = rt_stopwatch_new();

    // Start the stopwatch
    rt_stopwatch_start(sw);
    test_result("Start() sets IsRunning=true", rt_stopwatch_is_running(sw) != 0);

    // Let some time pass
    sleep_ms(50);

    // Stop the stopwatch
    rt_stopwatch_stop(sw);
    test_result("Stop() sets IsRunning=false", rt_stopwatch_is_running(sw) == 0);

    int64_t elapsed = rt_stopwatch_elapsed_ms(sw);
    test_result("Elapsed >= 40ms after 50ms sleep", elapsed >= 40);
    test_result("Elapsed stays within loaded-system tolerance", elapsed <= kShortSleepUpperBoundMs);

    // Verify time doesn't accumulate when stopped
    sleep_ms(50);
    int64_t elapsed2 = rt_stopwatch_elapsed_ms(sw);
    test_result("Time doesn't accumulate when stopped", elapsed2 == elapsed);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test that Start() is idempotent when already running.
static void test_start_idempotent() {
    printf("Testing Start() idempotent:\n");

    void *sw = rt_stopwatch_new();
    rt_stopwatch_start(sw);

    sleep_ms(30);

    // Calling Start() again should not reset the timer
    rt_stopwatch_start(sw);

    sleep_ms(30);

    rt_stopwatch_stop(sw);

    int64_t elapsed = rt_stopwatch_elapsed_ms(sw);
    // Should have approximately 60ms total, not 30ms
    test_result("Start() while running doesn't reset (>= 50ms)", elapsed >= 50);
    test_result("Start() while running stays within loaded-system tolerance",
                elapsed <= kShortSleepUpperBoundMs);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test that Stop() is idempotent when already stopped.
static void test_stop_idempotent() {
    printf("Testing Stop() idempotent:\n");

    void *sw = rt_stopwatch_new();
    rt_stopwatch_start(sw);
    sleep_ms(30);
    rt_stopwatch_stop(sw);

    int64_t elapsed1 = rt_stopwatch_elapsed_ms(sw);

    // Calling Stop() again should not change anything
    rt_stopwatch_stop(sw);

    int64_t elapsed2 = rt_stopwatch_elapsed_ms(sw);
    test_result("Stop() while stopped doesn't change elapsed", elapsed1 == elapsed2);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test Reset() behavior.
static void test_reset() {
    printf("Testing Reset():\n");

    void *sw = rt_stopwatch_new();
    rt_stopwatch_start(sw);
    sleep_ms(30);

    rt_stopwatch_reset(sw);

    test_result("Reset() sets IsRunning=false", rt_stopwatch_is_running(sw) == 0);
    test_result("Reset() clears elapsed time", rt_stopwatch_elapsed_ms(sw) == 0);
    test_result("Reset() clears elapsed time (ns)", rt_stopwatch_elapsed_ns(sw) == 0);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test Restart() behavior.
static void test_restart() {
    printf("Testing Restart():\n");

    void *sw = rt_stopwatch_new();
    rt_stopwatch_start(sw);
    sleep_ms(50);

    // Restart should reset and start in one operation
    rt_stopwatch_restart(sw);

    // Elapsed should be near zero
    int64_t elapsed = rt_stopwatch_elapsed_ms(sw);
    test_result("Restart() resets elapsed", elapsed < kRestartUpperBoundMs);
    test_result("Restart() sets IsRunning=true", rt_stopwatch_is_running(sw) != 0);

    sleep_ms(30);
    elapsed = rt_stopwatch_elapsed_ms(sw);
    test_result("Restart() allows accumulation (>= 25ms)", elapsed >= 25);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test accumulation across multiple start/stop cycles.
static void test_accumulation() {
    printf("Testing accumulation:\n");

    void *sw = rt_stopwatch_new();

    // First interval
    rt_stopwatch_start(sw);
    sleep_ms(30);
    rt_stopwatch_stop(sw);
    int64_t elapsed1 = rt_stopwatch_elapsed_ms(sw);

    // Second interval
    rt_stopwatch_start(sw);
    sleep_ms(30);
    rt_stopwatch_stop(sw);
    int64_t elapsed2 = rt_stopwatch_elapsed_ms(sw);

    // Third interval
    rt_stopwatch_start(sw);
    sleep_ms(30);
    rt_stopwatch_stop(sw);
    int64_t elapsed3 = rt_stopwatch_elapsed_ms(sw);

    test_result("First interval >= 25ms", elapsed1 >= 25);
    test_result("Second interval > first", elapsed2 > elapsed1);
    test_result("Third interval > second", elapsed3 > elapsed2);
    test_result("Total accumulation >= 75ms", elapsed3 >= 75);
    test_result("Total accumulation stays within loaded-system tolerance",
                elapsed3 <= kLongSleepUpperBoundMs);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test reading elapsed while running vs stopped.
static void test_elapsed_while_running() {
    printf("Testing elapsed while running:\n");

    void *sw = rt_stopwatch_new();
    rt_stopwatch_start(sw);

    sleep_ms(30);
    int64_t e1 = rt_stopwatch_elapsed_ms(sw);

    sleep_ms(30);
    int64_t e2 = rt_stopwatch_elapsed_ms(sw);

    test_result("Elapsed increases while running", e2 > e1);

    rt_stopwatch_stop(sw);
    int64_t e3 = rt_stopwatch_elapsed_ms(sw);

    sleep_ms(30);
    int64_t e4 = rt_stopwatch_elapsed_ms(sw);

    test_result("Elapsed stable after stop", e4 == e3);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test different time units.
static void test_time_units() {
    printf("Testing time units:\n");

    void *sw = rt_stopwatch_new();
    rt_stopwatch_start(sw);
    sleep_ms(100);
    rt_stopwatch_stop(sw);

    int64_t ms = rt_stopwatch_elapsed_ms(sw);
    int64_t us = rt_stopwatch_elapsed_us(sw);
    int64_t ns = rt_stopwatch_elapsed_ns(sw);

    test_result("ElapsedMs >= 80", ms >= 80);
    test_result("ElapsedMs stays within loaded-system tolerance", ms <= kLongSleepUpperBoundMs);
    test_result("ElapsedUs >= 80000", us >= 80000);
    test_result("ElapsedUs stays within loaded-system tolerance",
                us <= (kLongSleepUpperBoundMs * 1000));
    test_result("ElapsedNs >= 80000000", ns >= 80000000);
    test_result("ElapsedNs stays within loaded-system tolerance",
                ns <= (kLongSleepUpperBoundMs * 1000000));

    // Verify relationships
    test_result("ElapsedUs ~= ElapsedMs * 1000", us >= (ms * 990) && us <= (ms * 1010));
    test_result("ElapsedNs ~= ElapsedUs * 1000", ns >= (us * 990) && ns <= (us * 1010));
    rt_release_obj(sw);

    printf("\n");
}

static void test_null_receiver_traps() {
    printf("Testing null receiver traps:\n");

    EXPECT_TRAP(rt_stopwatch_start(nullptr));
    EXPECT_TRAP(rt_stopwatch_stop(nullptr));
    EXPECT_TRAP(rt_stopwatch_reset(nullptr));
    EXPECT_TRAP(rt_stopwatch_restart(nullptr));
    EXPECT_TRAP(rt_stopwatch_elapsed_ns(nullptr));
    EXPECT_TRAP(rt_stopwatch_elapsed_us(nullptr));
    EXPECT_TRAP(rt_stopwatch_elapsed_ms(nullptr));
    EXPECT_TRAP(rt_stopwatch_is_running(nullptr));

    test_result("Null receiver methods trap", true);
    printf("\n");
}

/// @brief Hammer the timestamp path from many threads on first use (VDOC-224).
/// @details The QPC frequency cache is a process-global slot resolved lazily on
///          the first `get_timestamp_*` call. Spawning threads that all read
///          `elapsed_ns` concurrently before any prior read races that lazy
///          initialization — exactly the Windows scenario the atomic cache
///          fixes. On POSIX this exercises the monotonic path's thread safety.
///          Each thread reads its own stopwatch, so there is no concurrent GC
///          allocation, isolating the shared frequency cache. Every reading must
///          be non-negative and non-decreasing per thread; a torn frequency
///          would surface as a negative or wildly out-of-order value.
static void test_concurrent_first_use() {
    printf("Testing concurrent first-use timestamp race:\n");

    constexpr int kThreads = 8;
    constexpr int kReads = 2000;

    void *watches[kThreads];
    for (int i = 0; i < kThreads; ++i) {
        watches[i] = rt_stopwatch_start_new();
        assert(watches[i] != nullptr);
    }

    std::atomic<bool> ready{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() {
            while (!ready.load(std::memory_order_acquire)) {
                // Spin so every thread reaches the first read at roughly the
                // same moment, maximizing the first-use overlap.
            }
            int64_t prev = 0;
            for (int r = 0; r < kReads; ++r) {
                int64_t now = rt_stopwatch_elapsed_ns(watches[i]);
                if (now < 0 || now < prev)
                    failures.fetch_add(1, std::memory_order_relaxed);
                prev = now;
            }
        });
    }

    ready.store(true, std::memory_order_release);
    for (auto &t : threads)
        t.join();

    test_result("no negative or out-of-order readings", failures.load() == 0);
    printf("\n");
}

/// @brief Entry point for Stopwatch tests.
int main() {
    printf("=== RT Stopwatch Tests ===\n\n");

    test_concurrent_first_use();
    test_new();
    test_start_new();
    test_start_stop();
    test_start_idempotent();
    test_stop_idempotent();
    test_reset();
    test_restart();
    test_accumulation();
    test_elapsed_while_running();
    test_time_units();
    test_null_receiver_traps();

    printf("All Stopwatch tests passed!\n");
    return 0;
}
