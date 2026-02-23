//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_ratelimit.h"

#include <cassert>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_new_limiter()
{
    void *rl = rt_ratelimit_new(10, 5.0);
    assert(rl != NULL);
    assert(rt_ratelimit_get_max(rl) == 10);
    assert(rt_ratelimit_get_rate(rl) == 5.0);
    assert(rt_ratelimit_available(rl) == 10);
}

static void test_acquire_single()
{
    void *rl = rt_ratelimit_new(5, 1.0);

    // Should succeed 5 times
    for (int i = 0; i < 5; i++)
    {
        assert(rt_ratelimit_try_acquire(rl) == 1);
    }

    // 6th should fail
    assert(rt_ratelimit_try_acquire(rl) == 0);
    assert(rt_ratelimit_available(rl) == 0);
}

static void test_acquire_n()
{
    void *rl = rt_ratelimit_new(10, 1.0);

    // Acquire 7 at once
    assert(rt_ratelimit_try_acquire_n(rl, 7) == 1);
    assert(rt_ratelimit_available(rl) == 3);

    // Try to acquire 5 (only 3 available) - should fail atomically
    assert(rt_ratelimit_try_acquire_n(rl, 5) == 0);
    assert(rt_ratelimit_available(rl) == 3); // unchanged

    // Acquire remaining 3
    assert(rt_ratelimit_try_acquire_n(rl, 3) == 1);
    assert(rt_ratelimit_available(rl) == 0);
}

static void test_reset()
{
    void *rl = rt_ratelimit_new(5, 1.0);

    // Drain all tokens
    for (int i = 0; i < 5; i++)
        rt_ratelimit_try_acquire(rl);

    assert(rt_ratelimit_available(rl) == 0);

    // Reset to full
    rt_ratelimit_reset(rl);
    assert(rt_ratelimit_available(rl) == 5);
}

static void test_defaults_for_invalid_params()
{
    // Zero/negative values should default to 1
    void *rl = rt_ratelimit_new(0, 0.0);
    assert(rt_ratelimit_get_max(rl) == 1);
    assert(rt_ratelimit_get_rate(rl) == 1.0);
}

static void test_acquire_n_invalid()
{
    void *rl = rt_ratelimit_new(10, 1.0);

    // n <= 0 should return 0
    assert(rt_ratelimit_try_acquire_n(rl, 0) == 0);
    assert(rt_ratelimit_try_acquire_n(rl, -1) == 0);

    // Tokens should be unchanged
    assert(rt_ratelimit_available(rl) == 10);
}

static void test_null_safety()
{
    assert(rt_ratelimit_try_acquire(NULL) == 0);
    assert(rt_ratelimit_try_acquire_n(NULL, 1) == 0);
    assert(rt_ratelimit_available(NULL) == 0);
    assert(rt_ratelimit_get_max(NULL) == 0);
    assert(rt_ratelimit_get_rate(NULL) == 0.0);
    rt_ratelimit_reset(NULL); // should not crash
}

/// @brief Regression test for RC-9: token count must never go negative.
///
/// After many acquire/refill cycles the floating-point token accumulator
/// can drift slightly below zero due to rounding. The RC-9 fix clamps
/// tokens to 0.0 after any subtraction so that rt_ratelimit_available()
/// always returns a non-negative value.
static void test_tokens_never_go_negative()
{
    // High refill rate creates many fractional-token scenarios in a tight loop.
    void *rl = rt_ratelimit_new(5, 1000.0);
    assert(rl != NULL);

    for (int i = 0; i < 10000; i++)
    {
        rt_ratelimit_try_acquire(rl);
        int64_t avail = rt_ratelimit_available(rl);
        // RC-9: must be >= 0 regardless of floating-point drift
        assert(avail >= 0 && "available() went negative — RC-9 regression");
    }
}

int main()
{
    test_new_limiter();
    test_acquire_single();
    test_acquire_n();
    test_reset();
    test_defaults_for_invalid_params();
    test_acquire_n_invalid();
    test_null_safety();
    test_tokens_never_go_negative();
    return 0;
}
