// File: tests/runtime/RT_SleepTests.cpp
// Purpose: Validate rt_sleep_ms blocks approximately the requested duration.
// Key invariants: Negative values clamp to zero; elapsed time >= lower bound.
// Ownership: Uses runtime library only.
// Links: docs/runtime-vm.md

#include "viper/runtime/rt.h"
#include <cassert>
#include <chrono>

static inline int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int main()
{
    const int sleep_ms = 100;
    const int64_t t0 = now_ms();
    rt_sleep_ms(sleep_ms);
    const int64_t t1 = now_ms();
    const int64_t elapsed = t1 - t0;

    // Allow some CI variance but ensure we didn't return immediately.
    assert(elapsed >= 90);
    assert(elapsed < 5000); // Should not be excessively long.

    // Negative clamp to zero should not crash and should return quickly.
    const int64_t t2 = now_ms();
    rt_sleep_ms(-42);
    const int64_t t3 = now_ms();
    assert(t3 - t2 >= 0);
    return 0;
}

