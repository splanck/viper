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

#include <cassert>
#include <cstdio>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void sleep_ms(int ms)
{
    Sleep((DWORD)ms);
}
#else
#include <time.h>

static void sleep_ms(int ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}
#endif

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static void rt_release_obj(void *p)
{
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

/// @brief Test creating a new stopwatch.
static void test_new()
{
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
static void test_start_new()
{
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
static void test_start_stop()
{
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
    test_result("Elapsed <= 100ms after 50ms sleep", elapsed <= 100);

    // Verify time doesn't accumulate when stopped
    sleep_ms(50);
    int64_t elapsed2 = rt_stopwatch_elapsed_ms(sw);
    test_result("Time doesn't accumulate when stopped", elapsed2 == elapsed);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test that Start() is idempotent when already running.
static void test_start_idempotent()
{
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
    test_result("Start() while running doesn't reset (<= 100ms)", elapsed <= 100);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test that Stop() is idempotent when already stopped.
static void test_stop_idempotent()
{
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
static void test_reset()
{
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
static void test_restart()
{
    printf("Testing Restart():\n");

    void *sw = rt_stopwatch_new();
    rt_stopwatch_start(sw);
    sleep_ms(50);

    // Restart should reset and start in one operation
    rt_stopwatch_restart(sw);

    // Elapsed should be near zero
    int64_t elapsed = rt_stopwatch_elapsed_ms(sw);
    test_result("Restart() resets elapsed (< 10ms)", elapsed < 10);
    test_result("Restart() sets IsRunning=true", rt_stopwatch_is_running(sw) != 0);

    sleep_ms(30);
    elapsed = rt_stopwatch_elapsed_ms(sw);
    test_result("Restart() allows accumulation (>= 25ms)", elapsed >= 25);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test accumulation across multiple start/stop cycles.
static void test_accumulation()
{
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
    test_result("Total accumulation <= 150ms", elapsed3 <= 150);
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Test reading elapsed while running vs stopped.
static void test_elapsed_while_running()
{
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
static void test_time_units()
{
    printf("Testing time units:\n");

    void *sw = rt_stopwatch_new();
    rt_stopwatch_start(sw);
    sleep_ms(100);
    rt_stopwatch_stop(sw);

    int64_t ms = rt_stopwatch_elapsed_ms(sw);
    int64_t us = rt_stopwatch_elapsed_us(sw);
    int64_t ns = rt_stopwatch_elapsed_ns(sw);

    test_result("ElapsedMs >= 80", ms >= 80);
    test_result("ElapsedMs <= 150", ms <= 150);
    test_result("ElapsedUs >= 80000", us >= 80000);
    test_result("ElapsedUs <= 150000", us <= 150000);
    test_result("ElapsedNs >= 80000000", ns >= 80000000);
    test_result("ElapsedNs <= 150000000", ns <= 150000000);

    // Verify relationships
    test_result("ElapsedUs ~= ElapsedMs * 1000", us >= (ms * 990) && us <= (ms * 1010));
    test_result("ElapsedNs ~= ElapsedUs * 1000", ns >= (us * 990) && ns <= (us * 1010));
    rt_release_obj(sw);

    printf("\n");
}

/// @brief Entry point for Stopwatch tests.
int main()
{
    printf("=== RT Stopwatch Tests ===\n\n");

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

    printf("All Stopwatch tests passed!\n");
    return 0;
}
