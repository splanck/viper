//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_debounce.h"

#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_debounce_create()
{
    void *d = rt_debounce_new(100);
    assert(d != NULL);
    assert(rt_debounce_get_delay(d) == 100);
    assert(rt_debounce_get_signal_count(d) == 0);
    assert(rt_debounce_is_ready(d) == 0); // Never signaled
}

static void test_debounce_signal()
{
    void *d = rt_debounce_new(10);
    rt_debounce_signal(d);
    assert(rt_debounce_get_signal_count(d) == 1);
    rt_debounce_signal(d);
    assert(rt_debounce_get_signal_count(d) == 2);
}

static void test_debounce_ready()
{
    void *d = rt_debounce_new(10); // 10ms delay
    rt_debounce_signal(d);
    // Should become ready after delay
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(rt_debounce_is_ready(d) == 1);
}

static void test_debounce_reset()
{
    void *d = rt_debounce_new(10);
    rt_debounce_signal(d);
    rt_debounce_reset(d);
    assert(rt_debounce_get_signal_count(d) == 0);
    assert(rt_debounce_is_ready(d) == 0);
}

static void test_throttle_create()
{
    void *t = rt_throttle_new(100);
    assert(t != NULL);
    assert(rt_throttle_get_interval(t) == 100);
    assert(rt_throttle_get_count(t) == 0);
}

static void test_throttle_try()
{
    void *t = rt_throttle_new(100);
    // First try should always succeed
    assert(rt_throttle_try(t) == 1);
    assert(rt_throttle_get_count(t) == 1);
    // Second try immediately should fail
    assert(rt_throttle_try(t) == 0);
    assert(rt_throttle_get_count(t) == 1);
}

static void test_throttle_after_interval()
{
    void *t = rt_throttle_new(10); // 10ms
    assert(rt_throttle_try(t) == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(rt_throttle_try(t) == 1);
    assert(rt_throttle_get_count(t) == 2);
}

static void test_throttle_reset()
{
    void *t = rt_throttle_new(1000);
    rt_throttle_try(t);
    assert(rt_throttle_can_proceed(t) == 0);
    rt_throttle_reset(t);
    assert(rt_throttle_can_proceed(t) == 1);
    assert(rt_throttle_get_count(t) == 0);
}

static void test_throttle_remaining()
{
    void *t = rt_throttle_new(100);
    assert(rt_throttle_remaining_ms(t) == 0); // Never used
    rt_throttle_try(t);
    int64_t remaining = rt_throttle_remaining_ms(t);
    assert(remaining > 0 && remaining <= 100);
}

static void test_null_safety()
{
    assert(rt_debounce_is_ready(NULL) == 0);
    assert(rt_debounce_get_delay(NULL) == 0);
    assert(rt_debounce_get_signal_count(NULL) == 0);
    rt_debounce_signal(NULL);
    rt_debounce_reset(NULL);

    assert(rt_throttle_try(NULL) == 0);
    assert(rt_throttle_can_proceed(NULL) == 0);
    assert(rt_throttle_get_interval(NULL) == 0);
    assert(rt_throttle_get_count(NULL) == 0);
    assert(rt_throttle_remaining_ms(NULL) == 0);
    rt_throttle_reset(NULL);
}

int main()
{
    test_debounce_create();
    test_debounce_signal();
    test_debounce_ready();
    test_debounce_reset();
    test_throttle_create();
    test_throttle_try();
    test_throttle_after_interval();
    test_throttle_reset();
    test_throttle_remaining();
    test_null_safety();
    return 0;
}
