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

    int64_t d0 = rt_retry_next_delay(p); // 100
    int64_t d1 = rt_retry_next_delay(p); // 200
    int64_t d2 = rt_retry_next_delay(p); // 400
    int64_t d3 = rt_retry_next_delay(p); // 800

    assert(d0 == 100);
    assert(d1 == 200);
    assert(d2 == 400);
    assert(d3 == 800);
    assert(rt_retry_is_exhausted(p) == 1);
}

static void test_exponential_cap()
{
    void *p = rt_retry_exponential(5, 100, 300);

    int64_t d0 = rt_retry_next_delay(p); // 100
    int64_t d1 = rt_retry_next_delay(p); // 200
    int64_t d2 = rt_retry_next_delay(p); // capped to 300
    int64_t d3 = rt_retry_next_delay(p); // capped to 300

    assert(d0 == 100);
    assert(d1 == 200);
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

int main()
{
    test_fixed_retry();
    test_exponential_retry();
    test_exponential_cap();
    test_reset();
    test_zero_retries();
    test_total_attempts();
    test_null_safety();
    return 0;
}
