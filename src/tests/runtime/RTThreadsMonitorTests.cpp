//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTThreadsMonitorTests.cpp
// Purpose: Validate FIFO-fair, re-entrant monitor semantics for Zanna.Threads.Monitor.
// Key invariants: PauseAll wakes waiters FIFO; WaitFor timeouts re-acquire fairly.
// Ownership/Lifetime: Uses runtime library and OS threads; skip on Windows.
// Links: src/runtime/threads/rt_monitor_posix.c,
//        src/runtime/threads/rt_monitor_win.c, docs/zannalib/threads.md
//
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include "rt_object.h"

#include "common/ProcessIsolation.hpp"
#include <atomic>
#include <cassert>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static void call_enter_null() {
    rt_monitor_enter(nullptr);
}

static void test_pause_all_fifo() {
    int obj_storage = 0;
    void *obj = &obj_storage;

    constexpr int kThreads = 6;
    std::vector<std::atomic<int>> stage(kThreads);
    for (auto &s : stage)
        s.store(0, std::memory_order_relaxed);

    std::vector<int> resumed;
    resumed.reserve(kThreads);
    std::mutex resumed_mu;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    rt_monitor_enter(obj);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() {
            rt_monitor_enter(obj);
            stage[i].store(1, std::memory_order_release);
            rt_monitor_wait(obj);
            {
                std::lock_guard<std::mutex> lock(resumed_mu);
                resumed.push_back(i);
            }
            rt_monitor_exit(obj);
        });

        rt_monitor_exit(obj);

        while (stage[i].load(std::memory_order_acquire) != 1) {
            std::this_thread::yield();
        }

        // This blocks until thread i releases the monitor via Wait().
        rt_monitor_enter(obj);
    }

    // All threads are now enqueued on the monitor's FIFO wait queue.
    rt_monitor_pause_all(obj);
    rt_monitor_exit(obj);

    for (auto &t : threads)
        t.join();

    assert(resumed.size() == static_cast<size_t>(kThreads));
    for (int i = 0; i < kThreads; ++i)
        assert(resumed[static_cast<size_t>(i)] == i);
}

static void test_wait_for_timeout() {
    int obj_storage = 0;
    void *obj = &obj_storage;
    rt_monitor_enter(obj);
    int8_t ok = rt_monitor_wait_for(obj, /*ms=*/10);
    assert(ok == 0);
    rt_monitor_exit(obj);
}

/// @brief Exercise independent monitor buckets concurrently.
/// @details Each thread repeatedly acquires a disjoint set of managed objects.
///          Correctness requires exact per-object counts, while the workload
///          ensures unrelated monitor lookups can proceed through different
///          table-lock stripes without sharing ownership state.
static void test_independent_monitor_table_concurrency() {
    constexpr int kThreads = 8;
    constexpr int kObjectsPerThread = 16;
    constexpr int kIterations = 250;
    constexpr int kObjectCount = kThreads * kObjectsPerThread;
    std::vector<void *> objects(kObjectCount);
    std::vector<int> counters(kObjectCount, 0);
    for (void *&object : objects) {
        object = rt_obj_new_i64(0, 1);
        assert(object != nullptr);
    }

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int thread_index = 0; thread_index < kThreads; ++thread_index) {
        workers.emplace_back([&, thread_index]() {
            const int first = thread_index * kObjectsPerThread;
            for (int iteration = 0; iteration < kIterations; ++iteration) {
                for (int offset = 0; offset < kObjectsPerThread; ++offset) {
                    const int index = first + offset;
                    rt_monitor_enter(objects[index]);
                    ++counters[index];
                    rt_monitor_exit(objects[index]);
                }
            }
        });
    }
    for (auto &worker : workers)
        worker.join();

    for (int index = 0; index < kObjectCount; ++index) {
        assert(counters[index] == kIterations);
        if (rt_obj_release_check0(objects[index]))
            rt_obj_free(objects[index]);
    }
}

int main(int argc, char *argv[]) {
    zanna::tests::registerChildFunction(call_enter_null);
    if (zanna::tests::dispatchChild(argc, argv))
        return 0;

    // Trap messages should be stable.
    auto result = zanna::tests::runIsolated(call_enter_null);
    assert(result.stderrText.find("Monitor.Enter: null object") != std::string::npos);

    test_wait_for_timeout();
    test_pause_all_fifo();
    test_independent_monitor_table_concurrency();
    return 0;
}
