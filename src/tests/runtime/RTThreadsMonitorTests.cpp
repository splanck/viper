//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTThreadsMonitorTests.cpp
// Purpose: Validate FIFO-fair, re-entrant monitor semantics for Viper.Threads.Monitor.
// Key invariants: PauseAll wakes waiters FIFO; WaitFor timeouts re-acquire fairly.
// Ownership/Lifetime: Uses runtime library and OS threads; skip on Windows.
//
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include <cassert>
#include <string>

#if defined(_WIN32)

int main()
{
    return 0;
}

#else

#include <atomic>
#include <mutex>
#include "tests/common/WaitCompat.hpp"
#include <thread>
#include "tests/common/PosixCompat.h"
#include <vector>

static std::string capture(void (*fn)())
{
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        fn();
        _exit(0);
    }
    close(fds[1]);
    char buf[256];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    if (n > 0)
        buf[n] = '\0';
    else
        buf[0] = '\0';
    int status = 0;
    waitpid(pid, &status, 0);
    return std::string(buf);
}

static void call_enter_null()
{
    rt_monitor_enter(nullptr);
}

static void test_pause_all_fifo()
{
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
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back(
            [&, i]()
            {
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

        while (stage[i].load(std::memory_order_acquire) != 1)
        {
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

static void test_wait_for_timeout()
{
    int obj_storage = 0;
    void *obj = &obj_storage;
    rt_monitor_enter(obj);
    int8_t ok = rt_monitor_wait_for(obj, /*ms=*/10);
    assert(ok == 0);
    rt_monitor_exit(obj);
}

int main()
{
    // Trap messages should be stable.
    std::string out = capture(call_enter_null);
    assert(out.find("Monitor.Enter: null object") != std::string::npos);

    test_wait_for_timeout();
    test_pause_all_fifo();
    return 0;
}

#endif
