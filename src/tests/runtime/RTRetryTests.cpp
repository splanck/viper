//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_retry.h"

#include <cassert>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_fixed_retry()
{
    void *p = rt_retry_new(3, 100);
    assert(rt_retry_can_retry(p) == 1);
    assert(rt_retry_get_max_retries(p) == 3);
    assert(rt_retry_get_attempt(p) == 0);

    // First retry
    int64_t delay = rt_retry_next_delay(p);
    assert(delay == 100);
    assert(rt_retry_get_attempt(p) == 1);

    // Second retry
    delay = rt_retry_next_delay(p);
    assert(delay == 100);

    // Third retry
    delay = rt_retry_next_delay(p);
    assert(delay == 100);

    // Exhausted
    assert(rt_retry_can_retry(p) == 0);
    assert(rt_retry_is_exhausted(p) == 1);
    delay = rt_retry_next_delay(p);
    assert(delay == -1);
}

static void test_exponential_retry()
{
    void *p = rt_retry_exponential(4, 100, 1000);

    int64_t d0 = rt_retry_next_delay(p); // 100 + jitter [0, 25]
    int64_t d1 = rt_retry_next_delay(p); // 200 + jitter [0, 50]
    int64_t d2 = rt_retry_next_delay(p); // 400 + jitter [0, 100]
    int64_t d3 = rt_retry_next_delay(p); // 800 + jitter [0, 200], capped at max=1000

    // RC-7: jitter adds rand() % (base/4 + 1) — verify values are in expected ranges
    assert(d0 >= 100 && d0 <= 125);
    assert(d1 >= 200 && d1 <= 250);
    assert(d2 >= 400 && d2 <= 500);
    assert(d3 >= 800 && d3 <= 1000);
    assert(rt_retry_is_exhausted(p) == 1);
}

static void test_exponential_cap()
{
    void *p = rt_retry_exponential(5, 100, 300);

    int64_t d0 = rt_retry_next_delay(p); // 100 + jitter, not yet capped
    int64_t d1 = rt_retry_next_delay(p); // 200 + jitter, not yet capped (max 250 < 300)
    int64_t d2 = rt_retry_next_delay(p); // 300 capped (jitter clamped to max)
    int64_t d3 = rt_retry_next_delay(p); // 300 capped

    assert(d0 >= 100 && d0 <= 125);
    assert(d1 >= 200 && d1 <= 250);
    assert(d2 == 300);
    assert(d3 == 300);
}

static void test_reset()
{
    void *p = rt_retry_new(2, 50);
    rt_retry_next_delay(p);
    rt_retry_next_delay(p);
    assert(rt_retry_is_exhausted(p) == 1);

    rt_retry_reset(p);
    assert(rt_retry_is_exhausted(p) == 0);
    assert(rt_retry_can_retry(p) == 1);
    assert(rt_retry_get_attempt(p) == 0);
}

static void test_zero_retries()
{
    void *p = rt_retry_new(0, 100);
    assert(rt_retry_can_retry(p) == 0);
    assert(rt_retry_is_exhausted(p) == 1);
    assert(rt_retry_next_delay(p) == -1);
}

static void test_total_attempts()
{
    void *p = rt_retry_new(3, 50);
    rt_retry_next_delay(p);
    rt_retry_next_delay(p);
    assert(rt_retry_get_total_attempts(p) == 2);
}

static void test_null_safety()
{
    assert(rt_retry_can_retry(NULL) == 0);
    assert(rt_retry_is_exhausted(NULL) == 1);
    assert(rt_retry_next_delay(NULL) == -1);
    assert(rt_retry_get_attempt(NULL) == 0);
    assert(rt_retry_get_max_retries(NULL) == 0);
    assert(rt_retry_get_total_attempts(NULL) == 0);
    rt_retry_reset(NULL);
}

/// @brief Regression test for RC-5/RC-6: all delays must be bounded.
///
/// RC-5: overflow guard prevents delay doubling past INT64_MAX.
/// RC-6: early-exit when delay already equals max_delay avoids redundant work.
///
/// Both are verified by running a policy with many retries and asserting
/// every returned delay is in [0, max_delay_ms].
static void test_exponential_delays_always_bounded()
{
    const int64_t max_delay = 5000;

    // Normal case: 20 retries with base=100ms; doubling will hit max quickly
    void *p = rt_retry_exponential(20, 100, max_delay);
    while (rt_retry_can_retry(p))
    {
        int64_t delay = rt_retry_next_delay(p);
        assert(delay >= 0 && "delay went negative — RC-5/RC-6 regression");
        assert(delay <= max_delay && "delay exceeded max_delay — RC-5/RC-6 regression");
    }

    // Note: RC-5 protects the doubling loop when delay > INT64_MAX/2. This guard
    // applies to unrealistically large base delays (billions of seconds) where
    // the jitter computation (int)jitter_range would also overflow int. The normal
    // bounded case above exercises RC-6 (early-exit cap) through regular doubling.
}

int main()
{
    test_fixed_retry();
    test_exponential_retry();
    test_exponential_cap();
    test_reset();
    test_zero_retries();
    test_total_attempts();
    test_null_safety();
    test_exponential_delays_always_bounded();
    return 0;
}
