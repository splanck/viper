//===----------------------------------------------------------------------===//
// RTThreadPoolTests.cpp - Tests for rt_threadpool (async task executor)
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" {
#include "rt_internal.h"
#include "rt_threadpool.h"
#include "rt_threads.h"
#include "rt_object.h"

void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}
}

//=============================================================================
// Shared test helpers (use C++ atomic since SafeI64 not on Windows)
//=============================================================================

static std::atomic<int64_t> g_counter{0};

static void init_counter() {
    g_counter.store(0);
}

extern "C" {
static void increment_task(void *arg) {
    (void)arg;
    g_counter.fetch_add(1);
}

static void slow_task(void *arg) {
    (void)arg;
    rt_thread_sleep(50);
    g_counter.fetch_add(1);
}
}

//=============================================================================
// Creation and properties
//=============================================================================

static void test_new() {
    void *pool = rt_threadpool_new(4);
    assert(pool != NULL);
    assert(rt_threadpool_get_size(pool) == 4);
    assert(rt_threadpool_get_pending(pool) == 0);
    assert(rt_threadpool_get_is_shutdown(pool) == 0);
    rt_threadpool_shutdown(pool);
}

static void test_new_clamp_min() {
    void *pool = rt_threadpool_new(0);
    assert(pool != NULL);
    assert(rt_threadpool_get_size(pool) == 1); // Clamped to 1
    rt_threadpool_shutdown(pool);
}

static void test_new_clamp_negative() {
    void *pool = rt_threadpool_new(-5);
    assert(pool != NULL);
    assert(rt_threadpool_get_size(pool) == 1); // Clamped to 1
    rt_threadpool_shutdown(pool);
}

//=============================================================================
// Task submission and execution
//=============================================================================

static void test_submit_and_wait() {
    init_counter();
    void *pool = rt_threadpool_new(2);

    int i;
    for (i = 0; i < 10; i++) {
        int8_t ok = rt_threadpool_submit(pool, (void *)increment_task, NULL);
        assert(ok == 1);
    }

    rt_threadpool_wait(pool);
    assert(g_counter.load() == 10);

    rt_threadpool_shutdown(pool);
}

static void test_submit_after_shutdown() {
    void *pool = rt_threadpool_new(2);
    rt_threadpool_shutdown(pool);

    assert(rt_threadpool_get_is_shutdown(pool) == 1);
    assert(rt_threadpool_submit(pool, (void *)increment_task, NULL) == 0);
}

static void test_submit_null_callback() {
    void *pool = rt_threadpool_new(2);
    assert(rt_threadpool_submit(pool, NULL, NULL) == 0);
    rt_threadpool_shutdown(pool);
}

//=============================================================================
// Wait with timeout
//=============================================================================

static void test_wait_for_success() {
    init_counter();
    void *pool = rt_threadpool_new(2);

    int i;
    for (i = 0; i < 5; i++)
        rt_threadpool_submit(pool, (void *)increment_task, NULL);

    int8_t done = rt_threadpool_wait_for(pool, 5000);
    assert(done == 1);
    assert(g_counter.load() == 5);

    rt_threadpool_shutdown(pool);
}

static void test_wait_for_immediate_check() {
    void *pool = rt_threadpool_new(2);

    // No tasks submitted -> immediately done
    int8_t done = rt_threadpool_wait_for(pool, 0);
    assert(done == 1);

    rt_threadpool_shutdown(pool);
}

//=============================================================================
// Shutdown modes
//=============================================================================

static void test_graceful_shutdown() {
    init_counter();
    void *pool = rt_threadpool_new(2);

    int i;
    for (i = 0; i < 5; i++)
        rt_threadpool_submit(pool, (void *)slow_task, NULL);

    // Graceful: waits for pending tasks to finish
    rt_threadpool_shutdown(pool);
    assert(rt_threadpool_get_is_shutdown(pool) == 1);
    assert(g_counter.load() == 5);
}

static void test_shutdown_now() {
    init_counter();
    void *pool = rt_threadpool_new(1); // 1 worker

    // Submit many slow tasks
    int i;
    for (i = 0; i < 20; i++)
        rt_threadpool_submit(pool, (void *)slow_task, NULL);

    // Immediate shutdown discards queue
    rt_threadpool_shutdown_now(pool);
    assert(rt_threadpool_get_is_shutdown(pool) == 1);
    // Not all 20 should have completed
    assert(g_counter.load() < 20);
}

//=============================================================================
// Null safety
//=============================================================================

static void test_null_safety() {
    assert(rt_threadpool_get_size(NULL) == 0);
    assert(rt_threadpool_get_pending(NULL) == 0);
    assert(rt_threadpool_get_active(NULL) == 0);
    assert(rt_threadpool_get_is_shutdown(NULL) == 1);
    assert(rt_threadpool_submit(NULL, (void *)increment_task, NULL) == 0);
    assert(rt_threadpool_wait_for(NULL, 100) == 1);

    rt_threadpool_wait(NULL);        // Should not crash
    rt_threadpool_shutdown(NULL);    // Should not crash
    rt_threadpool_shutdown_now(NULL); // Should not crash
}

//=============================================================================
// Concurrent stress test
//=============================================================================

static void test_concurrent_submitters() {
    init_counter();
    void *pool = rt_threadpool_new(4);
    const int TASKS_PER_THREAD = 25;
    const int NUM_THREADS = 4;

    std::thread threads[NUM_THREADS];
    int i;
    for (i = 0; i < NUM_THREADS; i++) {
        threads[i] = std::thread([pool]() {
            for (int j = 0; j < TASKS_PER_THREAD; j++)
                rt_threadpool_submit(pool, (void *)increment_task, NULL);
        });
    }

    for (i = 0; i < NUM_THREADS; i++)
        threads[i].join();

    rt_threadpool_wait(pool);
    assert(g_counter.load() == NUM_THREADS * TASKS_PER_THREAD);

    rt_threadpool_shutdown(pool);
}

int main() {
    test_new();
    test_new_clamp_min();
    test_new_clamp_negative();
    test_submit_and_wait();
    test_submit_after_shutdown();
    test_submit_null_callback();
    test_wait_for_success();
    test_wait_for_immediate_check();
    test_graceful_shutdown();
    test_shutdown_now();
    test_null_safety();
    test_concurrent_submitters();

    printf("ThreadPool tests: all passed\n");
    return 0;
}
