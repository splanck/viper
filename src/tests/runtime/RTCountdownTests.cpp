//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCountdownTests.cpp
// Purpose: Tests for Viper.Time.Countdown interval timing functions.
//
//===----------------------------------------------------------------------===//

#include "rt_countdown.h"
#include "rt_internal.h"
#include "tests/common/PosixCompat.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctime>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Basic Creation Tests
// ============================================================================

static void test_new_countdown()
{
    void *cd = rt_countdown_new(1000);
    assert(cd != nullptr);
    assert(rt_countdown_interval(cd) == 1000);
    assert(rt_countdown_elapsed(cd) == 0);
    assert(rt_countdown_remaining(cd) == 1000);
    assert(rt_countdown_expired(cd) == 0);
    assert(rt_countdown_is_running(cd) == 0);

    printf("test_new_countdown: PASSED\n");
}

static void test_new_zero_interval()
{
    void *cd = rt_countdown_new(0);
    assert(cd != nullptr);
    assert(rt_countdown_interval(cd) == 0);
    // With 0 interval, should be immediately expired
    assert(rt_countdown_remaining(cd) == 0);
    assert(rt_countdown_expired(cd) == 1);

    printf("test_new_zero_interval: PASSED\n");
}

static void test_new_negative_interval()
{
    // Negative intervals should be clamped to 0
    void *cd = rt_countdown_new(-100);
    assert(cd != nullptr);
    assert(rt_countdown_interval(cd) == 0);

    printf("test_new_negative_interval: PASSED\n");
}

// ============================================================================
// Start/Stop Tests
// ============================================================================

static void test_start_stop()
{
    void *cd = rt_countdown_new(1000);

    // Initially stopped
    assert(rt_countdown_is_running(cd) == 0);

    // Start
    rt_countdown_start(cd);
    assert(rt_countdown_is_running(cd) == 1);

    // Start again (no effect)
    rt_countdown_start(cd);
    assert(rt_countdown_is_running(cd) == 1);

    // Stop
    rt_countdown_stop(cd);
    assert(rt_countdown_is_running(cd) == 0);

    // Stop again (no effect)
    rt_countdown_stop(cd);
    assert(rt_countdown_is_running(cd) == 0);

    printf("test_start_stop: PASSED\n");
}

// ============================================================================
// Elapsed Time Tests
// ============================================================================

static void test_elapsed_time()
{
    void *cd = rt_countdown_new(100);

    // Elapsed should be 0 before starting
    assert(rt_countdown_elapsed(cd) == 0);

    // Start and wait a bit
    rt_countdown_start(cd);

    // Small sleep to accumulate time (10ms)
    struct timespec ts = {0, 10000000}; // 10ms
    nanosleep(&ts, nullptr);

    // Should have some elapsed time
    int64_t elapsed = rt_countdown_elapsed(cd);
    assert(elapsed >= 5); // At least 5ms (allowing for timing variance)

    // Remaining should decrease
    int64_t remaining = rt_countdown_remaining(cd);
    assert(remaining <= 100 - 5);

    printf("test_elapsed_time: PASSED\n");
}

// ============================================================================
// Reset Tests
// ============================================================================

static void test_reset()
{
    void *cd = rt_countdown_new(1000);

    // Start and accumulate some time
    rt_countdown_start(cd);

    struct timespec ts = {0, 10000000}; // 10ms
    nanosleep(&ts, nullptr);

    // Reset
    rt_countdown_reset(cd);

    // Should be back to initial state
    assert(rt_countdown_elapsed(cd) == 0);
    assert(rt_countdown_is_running(cd) == 0);
    assert(rt_countdown_remaining(cd) == 1000);

    printf("test_reset: PASSED\n");
}

// ============================================================================
// Interval Tests
// ============================================================================

static void test_set_interval()
{
    void *cd = rt_countdown_new(1000);

    assert(rt_countdown_interval(cd) == 1000);

    rt_countdown_set_interval(cd, 500);
    assert(rt_countdown_interval(cd) == 500);

    // Negative should be clamped to 0
    rt_countdown_set_interval(cd, -100);
    assert(rt_countdown_interval(cd) == 0);

    printf("test_set_interval: PASSED\n");
}

// ============================================================================
// Expiration Tests
// ============================================================================

static void test_expiration()
{
    // Create countdown with very short interval (20ms)
    void *cd = rt_countdown_new(20);

    assert(rt_countdown_expired(cd) == 0);

    rt_countdown_start(cd);

    // Wait for expiration
    struct timespec ts = {0, 30000000}; // 30ms
    nanosleep(&ts, nullptr);

    // Should be expired now
    assert(rt_countdown_expired(cd) == 1);
    assert(rt_countdown_remaining(cd) == 0);

    printf("test_expiration: PASSED\n");
}

// ============================================================================
// Accumulation Tests
// ============================================================================

static void test_accumulation()
{
    void *cd = rt_countdown_new(1000);

    // Start and run for 10ms
    rt_countdown_start(cd);
    struct timespec ts = {0, 10000000}; // 10ms
    nanosleep(&ts, nullptr);
    rt_countdown_stop(cd);

    int64_t elapsed1 = rt_countdown_elapsed(cd);

    // Start again for another 10ms
    rt_countdown_start(cd);
    nanosleep(&ts, nullptr);
    rt_countdown_stop(cd);

    int64_t elapsed2 = rt_countdown_elapsed(cd);

    // Elapsed should have accumulated
    assert(elapsed2 > elapsed1);

    printf("test_accumulation: PASSED\n");
}

// ============================================================================
// Wait Tests
// ============================================================================

static void test_wait_short()
{
    // Create countdown with 50ms interval
    void *cd = rt_countdown_new(50);

    // Wait should start the timer and block until expiration
    rt_countdown_wait(cd);

    // Should be expired after wait
    assert(rt_countdown_expired(cd) == 1);

    printf("test_wait_short: PASSED\n");
}

static void test_wait_already_running()
{
    // Create countdown with 50ms interval
    void *cd = rt_countdown_new(50);

    // Start manually
    rt_countdown_start(cd);

    // Wait should just wait for remaining time
    rt_countdown_wait(cd);

    // Should be expired after wait
    assert(rt_countdown_expired(cd) == 1);

    printf("test_wait_already_running: PASSED\n");
}

static void test_wait_already_expired()
{
    // Create countdown with 0ms interval (immediately expired)
    void *cd = rt_countdown_new(0);

    // Wait should return immediately
    rt_countdown_wait(cd);

    assert(rt_countdown_expired(cd) == 1);

    printf("test_wait_already_expired: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Time.Countdown Tests ===\n\n");

    // Basic creation
    test_new_countdown();
    test_new_zero_interval();
    test_new_negative_interval();

    // Start/Stop
    test_start_stop();

    // Elapsed time
    test_elapsed_time();

    // Reset
    test_reset();

    // Interval
    test_set_interval();

    // Expiration
    test_expiration();

    // Accumulation
    test_accumulation();

    // Wait
    test_wait_short();
    test_wait_already_running();
    test_wait_already_expired();

    printf("\nAll RTCountdownTests passed!\n");
    return 0;
}
