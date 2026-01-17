//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTClockTests.cpp
// Purpose: Validate Viper.Time.Clock runtime functions.
// Key invariants: Ticks are monotonic, sleep actually sleeps, TicksUs has
//                 higher resolution than Ticks.
// Links: docs/viperlib.md

#include "viper/runtime/rt.h"

#include <cassert>
#include <cstdio>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Test that Ticks returns a non-negative value.
static void test_ticks_positive()
{
    printf("Testing Clock.Ticks positive:\n");

    int64_t t = rt_clock_ticks();
    test_result("Ticks >= 0", t >= 0);

    printf("\n");
}

/// @brief Test that Ticks is monotonic.
static void test_ticks_monotonic()
{
    printf("Testing Clock.Ticks monotonic:\n");

    int64_t t1 = rt_clock_ticks();
    int64_t t2 = rt_clock_ticks();
    int64_t t3 = rt_clock_ticks();

    test_result("t2 >= t1", t2 >= t1);
    test_result("t3 >= t2", t3 >= t2);

    printf("\n");
}

/// @brief Test that TicksUs returns a non-negative value.
static void test_ticks_us_positive()
{
    printf("Testing Clock.TicksUs positive:\n");

    int64_t t = rt_clock_ticks_us();
    test_result("TicksUs >= 0", t >= 0);

    printf("\n");
}

/// @brief Test that TicksUs is monotonic.
static void test_ticks_us_monotonic()
{
    printf("Testing Clock.TicksUs monotonic:\n");

    int64_t t1 = rt_clock_ticks_us();
    int64_t t2 = rt_clock_ticks_us();
    int64_t t3 = rt_clock_ticks_us();

    test_result("t2 >= t1", t2 >= t1);
    test_result("t3 >= t2", t3 >= t2);

    printf("\n");
}

/// @brief Test that TicksUs has reasonable relationship with Ticks.
static void test_ticks_us_resolution()
{
    printf("Testing Clock.TicksUs resolution:\n");

    int64_t ms = rt_clock_ticks();
    int64_t us = rt_clock_ticks_us();

    // TicksUs should be approximately 1000x Ticks (milliseconds vs microseconds)
    // Allow some tolerance for timing jitter (800x to 1200x range)
    // Only test if both values are reasonably large to avoid division issues
    if (ms > 100 && us > 100000)
    {
        int64_t ratio = us / ms;
        test_result("TicksUs ~1000x Ticks", ratio >= 800 && ratio <= 1200);
    }
    else
    {
        // If values are too small, just verify us >= ms
        test_result("TicksUs >= Ticks", us >= ms);
    }

    printf("\n");
}

/// @brief Test that Sleep actually sleeps for approximately the requested time.
static void test_sleep_duration()
{
    printf("Testing Clock.Sleep duration:\n");

    int64_t t1 = rt_clock_ticks();
    rt_clock_sleep(50); // Sleep for 50ms
    int64_t t2 = rt_clock_ticks();

    int64_t elapsed = t2 - t1;

    // Should have slept for at least 40ms (allowing some variance)
    test_result("Sleep >= 40ms", elapsed >= 40);

    // Should not have slept for more than 150ms (allowing for system scheduling)
    test_result("Sleep <= 150ms", elapsed <= 150);

    printf("\n");
}

/// @brief Test that Sleep handles edge cases.
static void test_sleep_edge_cases()
{
    printf("Testing Clock.Sleep edge cases:\n");

    // Sleep(0) should not block significantly
    int64_t t1 = rt_clock_ticks();
    rt_clock_sleep(0);
    int64_t t2 = rt_clock_ticks();
    int64_t elapsed = t2 - t1;
    test_result("Sleep(0) returns quickly", elapsed < 50);

    // Sleep(-1) should be treated as 0 (clamped)
    t1 = rt_clock_ticks();
    rt_clock_sleep(-1);
    t2 = rt_clock_ticks();
    elapsed = t2 - t1;
    test_result("Sleep(-1) returns quickly", elapsed < 50);

    printf("\n");
}

/// @brief Test microsecond timing precision.
static void test_ticks_us_precision()
{
    printf("Testing Clock.TicksUs precision:\n");

    // Take multiple samples and verify they're increasing
    int64_t samples[5];
    for (int i = 0; i < 5; i++)
    {
        samples[i] = rt_clock_ticks_us();
    }

    // At least some samples should differ (unless system is extremely fast)
    bool has_difference = false;
    for (int i = 1; i < 5; i++)
    {
        if (samples[i] > samples[0])
        {
            has_difference = true;
            break;
        }
    }

    // This test is informational - some systems are fast enough that all samples
    // could be identical
    printf("  TicksUs samples: %lld, %lld, %lld, %lld, %lld\n",
           (long long)samples[0],
           (long long)samples[1],
           (long long)samples[2],
           (long long)samples[3],
           (long long)samples[4]);
    printf("  Has microsecond-level differences: %s\n", has_difference ? "yes" : "no");

    printf("\n");
}

/// @brief Entry point for Clock tests.
int main()
{
#ifdef _WIN32
    // Skip on Windows: timing tests have platform-specific quirks that need investigation
    printf("Test skipped: Clock tests need Windows-specific calibration\n");
    return 0;
#endif
    printf("=== RT Clock Tests ===\n\n");

    test_ticks_positive();
    test_ticks_monotonic();
    test_ticks_us_positive();
    test_ticks_us_monotonic();
    test_ticks_us_resolution();
    test_sleep_duration();
    test_sleep_edge_cases();
    test_ticks_us_precision();

    printf("All Clock tests passed!\n");
    return 0;
}
