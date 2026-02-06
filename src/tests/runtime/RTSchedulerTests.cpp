//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_scheduler.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>
#include <thread>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_new_scheduler()
{
    void *s = rt_scheduler_new();
    assert(s != NULL);
    assert(rt_scheduler_pending(s) == 0);
}

static void test_schedule_and_pending()
{
    void *s = rt_scheduler_new();
    rt_string t1 = rt_string_from_bytes("task1", 5);
    rt_string t2 = rt_string_from_bytes("task2", 5);

    rt_scheduler_schedule(s, t1, 1000); // 1 second from now
    assert(rt_scheduler_pending(s) == 1);

    rt_scheduler_schedule(s, t2, 2000);
    assert(rt_scheduler_pending(s) == 2);
}

static void test_cancel()
{
    void *s = rt_scheduler_new();
    rt_string t1 = rt_string_from_bytes("task1", 5);

    rt_scheduler_schedule(s, t1, 1000);
    assert(rt_scheduler_pending(s) == 1);

    int8_t cancelled = rt_scheduler_cancel(s, t1);
    assert(cancelled == 1);
    assert(rt_scheduler_pending(s) == 0);

    // Cancel non-existent
    cancelled = rt_scheduler_cancel(s, t1);
    assert(cancelled == 0);
}

static void test_is_due_not_ready()
{
    void *s = rt_scheduler_new();
    rt_string t1 = rt_string_from_bytes("task1", 5);

    rt_scheduler_schedule(s, t1, 5000);      // 5 seconds from now
    assert(rt_scheduler_is_due(s, t1) == 0); // not due yet

    // Non-existent task
    rt_string missing = rt_string_from_bytes("nope", 4);
    assert(rt_scheduler_is_due(s, missing) == 0);
}

static void test_immediate_due()
{
    void *s = rt_scheduler_new();
    rt_string t1 = rt_string_from_bytes("now", 3);

    rt_scheduler_schedule(s, t1, 0); // due immediately
    // Small sleep to ensure the monotonic clock advances
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(rt_scheduler_is_due(s, t1) == 1);
}

static void test_poll_returns_due()
{
    void *s = rt_scheduler_new();
    rt_string t1 = rt_string_from_bytes("fast", 4);
    rt_string t2 = rt_string_from_bytes("slow", 4);

    rt_scheduler_schedule(s, t1, 0);     // due immediately
    rt_scheduler_schedule(s, t2, 60000); // due in 60 seconds

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    void *due = rt_scheduler_poll(s);
    assert(due != NULL);

    int64_t count = rt_seq_len(due);
    assert(count == 1);

    // The fast task should be returned
    rt_string name = (rt_string)rt_seq_get(due, 0);
    assert(strcmp(rt_string_cstr(name), "fast") == 0);

    // Fast task removed, slow task still pending
    assert(rt_scheduler_pending(s) == 1);
}

static void test_clear()
{
    void *s = rt_scheduler_new();
    rt_string t1 = rt_string_from_bytes("a", 1);
    rt_string t2 = rt_string_from_bytes("b", 1);

    rt_scheduler_schedule(s, t1, 100);
    rt_scheduler_schedule(s, t2, 200);
    assert(rt_scheduler_pending(s) == 2);

    rt_scheduler_clear(s);
    assert(rt_scheduler_pending(s) == 0);
}

static void test_duplicate_name_replaces()
{
    void *s = rt_scheduler_new();
    rt_string t1 = rt_string_from_bytes("task", 4);

    rt_scheduler_schedule(s, t1, 1000);
    assert(rt_scheduler_pending(s) == 1);

    // Schedule again with same name - should replace
    rt_scheduler_schedule(s, t1, 2000);
    assert(rt_scheduler_pending(s) == 1);
}

static void test_null_safety()
{
    assert(rt_scheduler_pending(NULL) == 0);
    rt_scheduler_clear(NULL); // should not crash
}

int main()
{
    test_new_scheduler();
    test_schedule_and_pending();
    test_cancel();
    test_is_due_not_ready();
    test_immediate_due();
    test_poll_returns_due();
    test_clear();
    test_duplicate_name_replaces();
    test_null_safety();
    return 0;
}
