//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTThreadsThreadTests.cpp
// Purpose: Validate Viper.Threads.Thread and SafeI64 runtime primitives.
// Key invariants: Thread join/timeout semantics work; SafeI64 operations are thread-safe.
// Ownership/Lifetime: Uses runtime library and OS threads; skip on Windows.
//
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include "common/ProcessIsolation.hpp"
#include <atomic>
#include <cassert>
#include <string>
#include <thread>
#include <vector>

static void call_thread_start_null()
{
    (void)rt_thread_start(nullptr, nullptr);
}

static void call_thread_join_null()
{
    rt_thread_join(nullptr);
}

extern "C" void add_loop_entry(void *arg)
{
    void *cell = arg;
    for (int i = 0; i < 1000; ++i)
        (void)rt_safe_i64_add(cell, 1);
}

static void test_safe_i64_concurrent_add()
{
    void *cell = rt_safe_i64_new(0);
    assert(cell != nullptr);

    constexpr int kThreads = 4;
    std::vector<void *> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
        threads.push_back(rt_thread_start((void *)&add_loop_entry, cell));

    for (void *t : threads)
        rt_thread_join(t);

    int64_t final = rt_safe_i64_get(cell);
    assert(final == 1000LL * kThreads);
}

extern "C" void sleep_then_store(void *arg)
{
    auto *p = static_cast<std::atomic<int> *>(arg);
    rt_thread_sleep(50);
    p->store(1, std::memory_order_release);
}

static void test_thread_join_for_timeout()
{
    std::atomic<int> flag{0};
    void *t = rt_thread_start((void *)&sleep_then_store, &flag);
    assert(t != nullptr);

    int8_t done = rt_thread_join_for(t, /*ms=*/1);
    assert(done == 0);

    rt_thread_join(t);
    assert(flag.load(std::memory_order_acquire) == 1);
}

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    // Trap messages should be stable.
    auto result = viper::tests::runIsolated(call_thread_start_null);
    assert(result.stderrText.find("Thread.Start: null entry") != std::string::npos);

    result = viper::tests::runIsolated(call_thread_join_null);
    assert(result.stderrText.find("Thread.Join: null thread") != std::string::npos);

    test_thread_join_for_timeout();
    test_safe_i64_concurrent_add();
    return 0;
}
