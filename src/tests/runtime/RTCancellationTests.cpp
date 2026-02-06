//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_cancellation.h"
#include "rt_internal.h"

#include <cassert>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_basic()
{
    void *token = rt_cancellation_new();
    assert(rt_cancellation_is_cancelled(token) == 0);
    rt_cancellation_cancel(token);
    assert(rt_cancellation_is_cancelled(token) == 1);
}

static void test_reset()
{
    void *token = rt_cancellation_new();
    rt_cancellation_cancel(token);
    assert(rt_cancellation_is_cancelled(token) == 1);
    rt_cancellation_reset(token);
    assert(rt_cancellation_is_cancelled(token) == 0);
}

static void test_linked()
{
    void *parent = rt_cancellation_new();
    void *child = rt_cancellation_linked(parent);

    assert(rt_cancellation_check(child) == 0);

    rt_cancellation_cancel(parent);
    assert(rt_cancellation_check(child) == 1);
    assert(rt_cancellation_is_cancelled(child) == 0); // Child itself not cancelled
}

static void test_linked_self_cancel()
{
    void *parent = rt_cancellation_new();
    void *child = rt_cancellation_linked(parent);

    rt_cancellation_cancel(child);
    assert(rt_cancellation_check(child) == 1);
    assert(rt_cancellation_is_cancelled(parent) == 0); // Parent not affected
}

static void test_null_safety()
{
    assert(rt_cancellation_is_cancelled(NULL) == 0);
    assert(rt_cancellation_check(NULL) == 0);
    rt_cancellation_cancel(NULL);
    rt_cancellation_reset(NULL);
}

int main()
{
    test_basic();
    test_reset();
    test_linked();
    test_linked_self_cancel();
    test_null_safety();
    return 0;
}
