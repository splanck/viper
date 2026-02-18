//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTConcurrencyTests.c
// Purpose: Single-threaded correctness tests for concurrent map, queue, and
//          scheduler runtime primitives.
// Key invariants: Exercises the rt_obj_free protocol paths (free_entry,
//                 cq_finalizer, rt_concqueue_clear, rt_scheduler_poll) that
//                 were previously missing the follow-up rt_obj_free call after
//                 rt_obj_release_check0 returned 0.
// Ownership/Lifetime: Test objects are created with rt_obj_new_i64 and tracked
//                     manually. Strings are created/released per-test.
//

#include "viper/runtime/rt.h"

#include "rt_concmap.h"
#include "rt_concqueue.h"
#include "rt_scheduler.h"
#include "rt_seq.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helper: create a small runtime object to use as a map/queue value.
// The object has refcount 1 on return; caller is responsible for release.
// ---------------------------------------------------------------------------
static void *make_obj(int64_t tag)
{
    return rt_obj_new_i64(tag, (int64_t)sizeof(int64_t));
}

// ---------------------------------------------------------------------------
// Helper: build an rt_string from a C literal, refcount 1 on return.
// ---------------------------------------------------------------------------
static rt_string make_key(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

//=============================================================================
// ConcurrentMap tests
//=============================================================================

// Basic set and get round-trip.
static void test_concmap_set_get(void)
{
    void *map = rt_concmap_new();
    assert(map);

    void *v1 = make_obj(1);
    rt_string k1 = make_key("alpha");

    rt_concmap_set(map, k1, v1);
    assert(rt_concmap_len(map) == 1);
    assert(rt_concmap_get(map, k1) == v1);
    assert(rt_concmap_has(map, k1));

    // Release test objects
    rt_string_unref(k1);
    if (rt_obj_release_check0(v1))
        rt_obj_free(v1);
    if (rt_obj_release_check0(map))
        rt_obj_free(map);
}

// Insert many entries, verify count and retrieval.
static void test_concmap_many_entries(void)
{
    void *map = rt_concmap_new();
    assert(map);

    void *vals[32];
    rt_string keys[32];
    char buf[16];
    for (int i = 0; i < 32; i++)
    {
        snprintf(buf, sizeof(buf), "key%d", i);
        keys[i] = make_key(buf);
        vals[i] = make_obj((int64_t)i);
        rt_concmap_set(map, keys[i], vals[i]);
    }

    assert(rt_concmap_len(map) == 32);

    for (int i = 0; i < 32; i++)
    {
        assert(rt_concmap_get(map, keys[i]) == vals[i]);
    }

    // Cleanup keys and values (map retains its own refs)
    for (int i = 0; i < 32; i++)
    {
        rt_string_unref(keys[i]);
        if (rt_obj_release_check0(vals[i]))
            rt_obj_free(vals[i]);
    }

    // Release the map (exercises cm_clear_unlocked -> free_entry path)
    if (rt_obj_release_check0(map))
        rt_obj_free(map);
}

// Set the same key twice — exercises the free_entry path on the replaced value
// via rt_concmap_set's "update existing entry" branch.
static void test_concmap_set_replaces_value(void)
{
    void *map = rt_concmap_new();
    assert(map);

    rt_string k = make_key("x");
    void *v1 = make_obj(10);
    void *v2 = make_obj(20);

    rt_concmap_set(map, k, v1);
    assert(rt_concmap_get(map, k) == v1);

    // Replace v1 with v2 — the old value must be released correctly.
    rt_concmap_set(map, k, v2);
    assert(rt_concmap_len(map) == 1);
    assert(rt_concmap_get(map, k) == v2);

    rt_string_unref(k);
    if (rt_obj_release_check0(v1))
        rt_obj_free(v1);
    if (rt_obj_release_check0(v2))
        rt_obj_free(v2);
    if (rt_obj_release_check0(map))
        rt_obj_free(map);
}

// Remove an entry — exercises the rt_concmap_remove -> free_entry path.
static void test_concmap_remove(void)
{
    void *map = rt_concmap_new();
    assert(map);

    rt_string k = make_key("hello");
    void *v = make_obj(42);

    rt_concmap_set(map, k, v);
    assert(rt_concmap_len(map) == 1);

    int8_t removed = rt_concmap_remove(map, k);
    assert(removed == 1);
    assert(rt_concmap_len(map) == 0);
    assert(!rt_concmap_has(map, k));

    // Key not present — should return 0
    assert(rt_concmap_remove(map, k) == 0);

    rt_string_unref(k);
    if (rt_obj_release_check0(v))
        rt_obj_free(v);
    if (rt_obj_release_check0(map))
        rt_obj_free(map);
}

// Clear all entries — exercises cm_clear_unlocked -> free_entry for every node.
static void test_concmap_clear(void)
{
    void *map = rt_concmap_new();
    assert(map);

    void *vals[8];
    rt_string keys[8];
    char buf[8];
    for (int i = 0; i < 8; i++)
    {
        snprintf(buf, sizeof(buf), "k%d", i);
        keys[i] = make_key(buf);
        vals[i] = make_obj((int64_t)i);
        rt_concmap_set(map, keys[i], vals[i]);
    }
    assert(rt_concmap_len(map) == 8);

    rt_concmap_clear(map);
    assert(rt_concmap_len(map) == 0);
    assert(rt_concmap_is_empty(map));

    for (int i = 0; i < 8; i++)
    {
        rt_string_unref(keys[i]);
        if (rt_obj_release_check0(vals[i]))
            rt_obj_free(vals[i]);
    }
    if (rt_obj_release_check0(map))
        rt_obj_free(map);
}

//=============================================================================
// ConcurrentQueue tests
//=============================================================================

// Enqueue one item, verify try_dequeue returns it.
static void test_concqueue_enqueue_dequeue(void)
{
    void *q = rt_concqueue_new();
    assert(q);
    assert(rt_concqueue_is_empty(q));

    void *item = make_obj(99);
    rt_concqueue_enqueue(q, item);
    assert(rt_concqueue_len(q) == 1);
    assert(!rt_concqueue_is_empty(q));

    void *got = rt_concqueue_try_dequeue(q);
    assert(got == item);
    assert(rt_concqueue_len(q) == 0);
    assert(rt_concqueue_is_empty(q));

    // try_dequeue on empty returns NULL
    assert(rt_concqueue_try_dequeue(q) == NULL);

    if (rt_obj_release_check0(item))
        rt_obj_free(item);
    if (rt_obj_release_check0(q))
        rt_obj_free(q);
}

// Enqueue many items and verify FIFO dequeue order.
static void test_concqueue_fifo_order(void)
{
    void *q = rt_concqueue_new();
    assert(q);

    enum
    {
        N = 16
    };

    void *items[N];
    for (int i = 0; i < N; i++)
    {
        items[i] = make_obj((int64_t)i);
        rt_concqueue_enqueue(q, items[i]);
    }
    assert(rt_concqueue_len(q) == N);

    for (int i = 0; i < N; i++)
    {
        void *got = rt_concqueue_try_dequeue(q);
        assert(got == items[i]);
    }
    assert(rt_concqueue_len(q) == 0);

    for (int i = 0; i < N; i++)
    {
        if (rt_obj_release_check0(items[i]))
            rt_obj_free(items[i]);
    }
    if (rt_obj_release_check0(q))
        rt_obj_free(q);
}

// Clear a populated queue — exercises the rt_concqueue_clear path.
static void test_concqueue_clear(void)
{
    void *q = rt_concqueue_new();
    assert(q);

    void *vals[4];
    for (int i = 0; i < 4; i++)
    {
        vals[i] = make_obj((int64_t)i);
        rt_concqueue_enqueue(q, vals[i]);
    }
    assert(rt_concqueue_len(q) == 4);

    rt_concqueue_clear(q);
    assert(rt_concqueue_len(q) == 0);
    assert(rt_concqueue_is_empty(q));

    for (int i = 0; i < 4; i++)
    {
        if (rt_obj_release_check0(vals[i]))
            rt_obj_free(vals[i]);
    }
    if (rt_obj_release_check0(q))
        rt_obj_free(q);
}

// Destroy a non-empty queue — exercises the cq_finalizer path.
static void test_concqueue_destroy_nonempty(void)
{
    void *q = rt_concqueue_new();
    assert(q);

    void *vals[3];
    for (int i = 0; i < 3; i++)
    {
        vals[i] = make_obj((int64_t)i);
        rt_concqueue_enqueue(q, vals[i]);
    }

    // Release the queue without dequeuing — cq_finalizer must release each item.
    if (rt_obj_release_check0(q))
        rt_obj_free(q);

    // The items were retained by the queue; release our references.
    for (int i = 0; i < 3; i++)
    {
        if (rt_obj_release_check0(vals[i]))
            rt_obj_free(vals[i]);
    }
}

// Peek does not remove the front item.
static void test_concqueue_peek(void)
{
    void *q = rt_concqueue_new();
    assert(q);

    void *a = make_obj(1);
    void *b = make_obj(2);
    rt_concqueue_enqueue(q, a);
    rt_concqueue_enqueue(q, b);

    assert(rt_concqueue_peek(q) == a);
    assert(rt_concqueue_len(q) == 2);

    void *front = rt_concqueue_try_dequeue(q);
    assert(front == a);

    if (rt_obj_release_check0(a))
        rt_obj_free(a);
    if (rt_obj_release_check0(b))
        rt_obj_free(b);
    if (rt_obj_release_check0(q))
        rt_obj_free(q);
}

//=============================================================================
// Scheduler tests
//=============================================================================

// Schedule a task with zero delay and poll immediately — it must appear in the
// result seq. This also exercises the rt_scheduler_poll name-transfer path.
static void test_scheduler_poll_immediate(void)
{
    void *sched = rt_scheduler_new();
    assert(sched);
    assert(rt_scheduler_pending(sched) == 0);

    rt_string name = make_key("task1");
    rt_scheduler_schedule(sched, name, 0);
    assert(rt_scheduler_pending(sched) == 1);

    void *due = rt_scheduler_poll(sched);
    assert(due);
    // Exactly one task should be due
    assert(rt_seq_len(due) == 1);
    assert(rt_scheduler_pending(sched) == 0);

    // Release the due task name transferred to caller via seq
    rt_string task_name = (rt_string)rt_seq_get(due, 0);
    assert(task_name);
    rt_string_unref(task_name);

    rt_string_unref(name);

    if (rt_obj_release_check0(due))
        rt_obj_free(due);
    if (rt_obj_release_check0(sched))
        rt_obj_free(sched);
}

// Poll with no tasks returns an empty seq.
static void test_scheduler_poll_empty(void)
{
    void *sched = rt_scheduler_new();
    assert(sched);

    void *due = rt_scheduler_poll(sched);
    assert(due);
    assert(rt_seq_len(due) == 0);

    if (rt_obj_release_check0(due))
        rt_obj_free(due);
    if (rt_obj_release_check0(sched))
        rt_obj_free(sched);
}

// Schedule a task with a large delay — poll should NOT return it.
static void test_scheduler_future_task_not_due(void)
{
    void *sched = rt_scheduler_new();
    assert(sched);

    rt_string name = make_key("future");
    rt_scheduler_schedule(sched, name, 60000); // 60 seconds
    assert(rt_scheduler_pending(sched) == 1);
    assert(rt_scheduler_is_due(sched, name) == 0);

    void *due = rt_scheduler_poll(sched);
    assert(rt_seq_len(due) == 0);
    assert(rt_scheduler_pending(sched) == 1);

    // Cancel the task to free it
    rt_scheduler_cancel(sched, name);
    assert(rt_scheduler_pending(sched) == 0);

    rt_string_unref(name);
    if (rt_obj_release_check0(due))
        rt_obj_free(due);
    if (rt_obj_release_check0(sched))
        rt_obj_free(sched);
}

// Cancel a task by name.
static void test_scheduler_cancel(void)
{
    void *sched = rt_scheduler_new();
    assert(sched);

    rt_string name = make_key("cancelme");
    rt_scheduler_schedule(sched, name, 5000);
    assert(rt_scheduler_pending(sched) == 1);

    int8_t ok = rt_scheduler_cancel(sched, name);
    assert(ok == 1);
    assert(rt_scheduler_pending(sched) == 0);

    // Cancel again — should return 0
    assert(rt_scheduler_cancel(sched, name) == 0);

    rt_string_unref(name);
    if (rt_obj_release_check0(sched))
        rt_obj_free(sched);
}

// Schedule multiple zero-delay tasks; poll should return all of them.
static void test_scheduler_poll_multiple(void)
{
    void *sched = rt_scheduler_new();
    assert(sched);

    enum
    {
        NT = 5
    };

    rt_string names[NT];
    const char *name_strs[NT] = {"alpha", "beta", "gamma", "delta", "epsilon"};
    for (int i = 0; i < NT; i++)
    {
        names[i] = make_key(name_strs[i]);
        rt_scheduler_schedule(sched, names[i], 0);
    }
    assert(rt_scheduler_pending(sched) == NT);

    void *due = rt_scheduler_poll(sched);
    assert(rt_seq_len(due) == NT);
    assert(rt_scheduler_pending(sched) == 0);

    // Release name references transferred to caller
    for (int64_t i = 0; i < rt_seq_len(due); i++)
    {
        rt_string s = (rt_string)rt_seq_get(due, i);
        rt_string_unref(s);
    }

    for (int i = 0; i < NT; i++)
        rt_string_unref(names[i]);

    if (rt_obj_release_check0(due))
        rt_obj_free(due);
    if (rt_obj_release_check0(sched))
        rt_obj_free(sched);
}

// Clear scheduler with pending tasks.
static void test_scheduler_clear(void)
{
    void *sched = rt_scheduler_new();
    assert(sched);

    rt_string n1 = make_key("t1");
    rt_string n2 = make_key("t2");
    rt_scheduler_schedule(sched, n1, 1000);
    rt_scheduler_schedule(sched, n2, 2000);
    assert(rt_scheduler_pending(sched) == 2);

    rt_scheduler_clear(sched);
    assert(rt_scheduler_pending(sched) == 0);

    rt_string_unref(n1);
    rt_string_unref(n2);
    if (rt_obj_release_check0(sched))
        rt_obj_free(sched);
}

//=============================================================================
// Entry point
//=============================================================================

int main(void)
{
    // ConcurrentMap
    test_concmap_set_get();
    test_concmap_many_entries();
    test_concmap_set_replaces_value();
    test_concmap_remove();
    test_concmap_clear();

    // ConcurrentQueue
    test_concqueue_enqueue_dequeue();
    test_concqueue_fifo_order();
    test_concqueue_clear();
    test_concqueue_destroy_nonempty();
    test_concqueue_peek();

    // Scheduler
    test_scheduler_poll_empty();
    test_scheduler_poll_immediate();
    test_scheduler_future_task_not_due();
    test_scheduler_cancel();
    test_scheduler_poll_multiple();
    test_scheduler_clear();

    return 0;
}
