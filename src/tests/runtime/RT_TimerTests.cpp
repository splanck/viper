//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RT_TimerTests.cpp
// Purpose: Validate rt_timer_ms returns monotonic increasing values. 
// Key invariants: Timer values are non-decreasing; elapsed time measurements are reasonably
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"
#include <cassert>

int main()
{
    // Test 1: Call twice, second value >= first (monotonic)
    const int64_t t0 = rt_timer_ms();
    const int64_t t1 = rt_timer_ms();
    assert(t1 >= t0);

    // Test 2: Sleep ~50ms and verify elapsed time is reasonable
    const int64_t before_sleep = rt_timer_ms();
    rt_sleep_ms(50);
    const int64_t after_sleep = rt_timer_ms();

    const int64_t elapsed = after_sleep - before_sleep;

    // Allow tolerance for scheduling variance:
    // - Lower bound: 45ms (slightly less than requested to account for precision)
    // - Upper bound: 300ms (generous upper bound for CI environments)
    assert(elapsed >= 45);
    assert(elapsed < 300);

    // Test 3: Multiple rapid calls should be monotonic
    int64_t prev = rt_timer_ms();
    for (int i = 0; i < 100; ++i)
    {
        int64_t curr = rt_timer_ms();
        assert(curr >= prev);
        prev = curr;
    }

    return 0;
}
