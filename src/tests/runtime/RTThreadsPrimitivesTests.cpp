//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTThreadsPrimitivesTests.cpp
// Purpose: Validate Viper.Threads.Gate/Barrier/RwLock runtime primitives.
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_threads.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <csetjmp>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
thread_local jmp_buf g_trap_jmp;
thread_local const char *g_last_trap = nullptr;
thread_local bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0)                                                               \
        {                                                                                          \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static void test_gate_traps()
{
    EXPECT_TRAP(rt_gate_new(-1));
    assert(g_last_trap && std::string(g_last_trap).find("Gate.New: permits cannot be negative") !=
                              std::string::npos);

    void *gate = rt_gate_new(0);
    EXPECT_TRAP(rt_gate_leave_many(gate, -2));
    assert(g_last_trap && std::string(g_last_trap).find("Gate.Leave: count cannot be negative") !=
                              std::string::npos);

    EXPECT_TRAP(rt_gate_enter(nullptr));
    assert(g_last_trap &&
           std::string(g_last_trap).find("Gate.Enter: null object") != std::string::npos);
}

static void test_gate_basic_and_timeout()
{
    void *gate = rt_gate_new(2);
    assert(rt_gate_get_permits(gate) == 2);
    assert(rt_gate_try_enter(gate) == 1);
    assert(rt_gate_try_enter(gate) == 1);
    assert(rt_gate_try_enter(gate) == 0);
    assert(rt_gate_get_permits(gate) == 0);

    // Timed wait should time out when no permits become available.
    const int8_t ok = rt_gate_try_enter_for(gate, /*ms=*/20);
    assert(ok == 0);

    rt_gate_leave_many(gate, 2);
    assert(rt_gate_get_permits(gate) == 2);
}

static void test_gate_blocks_and_wakes()
{
    void *gate = rt_gate_new(0);
    std::atomic<bool> acquired = false;

    std::thread t(
        [&]
        {
            rt_gate_enter(gate);
            acquired.store(true, std::memory_order_release);
            rt_gate_leave(gate);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(acquired.load(std::memory_order_acquire) == false);

    rt_gate_leave(gate);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!acquired.load(std::memory_order_acquire))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    t.join();
}

static void test_barrier_basic()
{
    constexpr int64_t kParties = 6;
    void *barrier = rt_barrier_new(kParties);
    assert(rt_barrier_get_parties(barrier) == kParties);

    std::vector<int64_t> idx(kParties, -1);
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(kParties));

    for (int64_t i = 0; i < kParties; ++i)
    {
        threads.emplace_back([&, i] { idx[static_cast<size_t>(i)] = rt_barrier_arrive(barrier); });
    }

    for (auto &t : threads)
        t.join();

    std::vector<bool> seen(static_cast<size_t>(kParties), false);
    for (int64_t i = 0; i < kParties; ++i)
    {
        const int64_t v = idx[static_cast<size_t>(i)];
        assert(v >= 0 && v < kParties);
        assert(!seen[static_cast<size_t>(v)]);
        seen[static_cast<size_t>(v)] = true;
    }

    // Reset is allowed when no threads are waiting.
    rt_barrier_reset(barrier);
}

static void test_barrier_reset_traps_while_waiting()
{
    void *barrier = rt_barrier_new(2);

    std::thread t([&] { (void)rt_barrier_arrive(barrier); });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (rt_barrier_get_waiting(barrier) != 1)
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    EXPECT_TRAP(rt_barrier_reset(barrier));
    assert(g_last_trap && std::string(g_last_trap).find("Barrier.Reset: threads are waiting") !=
                              std::string::npos);

    (void)rt_barrier_arrive(barrier);
    t.join();
}

static void test_rwlock_traps()
{
    void *lock = rt_rwlock_new();

    EXPECT_TRAP(rt_rwlock_read_exit(lock));
    assert(g_last_trap &&
           std::string(g_last_trap).find("RwLock.ReadExit: exit without matching enter") !=
               std::string::npos);

    EXPECT_TRAP(rt_rwlock_write_exit(lock));
    assert(g_last_trap &&
           std::string(g_last_trap).find("RwLock.WriteExit: exit without matching enter") !=
               std::string::npos);
}

static void test_rwlock_writer_preference()
{
    void *lock = rt_rwlock_new();

    std::atomic<bool> reader1_acquired = false;
    std::atomic<bool> reader1_release = false;
    std::atomic<bool> writer_started = false;
    std::atomic<bool> writer_acquired = false;
    std::atomic<bool> writer_release = false;
    std::atomic<bool> reader2_acquired = false;

    std::thread reader1(
        [&]
        {
            rt_rwlock_read_enter(lock);
            reader1_acquired.store(true, std::memory_order_release);
            while (!reader1_release.load(std::memory_order_acquire))
                std::this_thread::yield();
            rt_rwlock_read_exit(lock);
        });

    std::thread writer(
        [&]
        {
            while (!reader1_acquired.load(std::memory_order_acquire))
                std::this_thread::yield();
            writer_started.store(true, std::memory_order_release);
            rt_rwlock_write_enter(lock);
            writer_acquired.store(true, std::memory_order_release);
            while (!writer_release.load(std::memory_order_acquire))
                std::this_thread::yield();
            rt_rwlock_write_exit(lock);
        });

    while (!reader1_acquired.load(std::memory_order_acquire))
        std::this_thread::yield();
    while (!writer_started.load(std::memory_order_acquire))
        std::this_thread::yield();

    // Give the writer a moment to enqueue before starting reader2.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::thread reader2(
        [&]
        {
            rt_rwlock_read_enter(lock);
            reader2_acquired.store(true, std::memory_order_release);
            rt_rwlock_read_exit(lock);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(writer_acquired.load(std::memory_order_acquire) == false);
    assert(reader2_acquired.load(std::memory_order_acquire) == false);

    reader1_release.store(true, std::memory_order_release);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!writer_acquired.load(std::memory_order_acquire))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    // With writer preference, reader2 must not acquire before writer.
    assert(reader2_acquired.load(std::memory_order_acquire) == false);

    writer_release.store(true, std::memory_order_release);

    while (!reader2_acquired.load(std::memory_order_acquire))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    reader1.join();
    writer.join();
    reader2.join();
}

static void test_rwlock_write_exit_non_owner_traps()
{
    void *lock = rt_rwlock_new();
    rt_rwlock_write_enter(lock);

    std::atomic<const char *> trap_msg = nullptr;
    std::thread t(
        [&]
        {
            EXPECT_TRAP(rt_rwlock_write_exit(lock));
            trap_msg.store(g_last_trap, std::memory_order_release);
        });
    t.join();

    const char *msg = trap_msg.load(std::memory_order_acquire);
    assert(msg && std::string(msg).find("RwLock.WriteExit: not owner") != std::string::npos);

    rt_rwlock_write_exit(lock);
}

int main()
{
    test_gate_traps();
    test_gate_basic_and_timeout();
    test_gate_blocks_and_wakes();

    test_barrier_basic();
    test_barrier_reset_traps_while_waiting();

    test_rwlock_traps();
    test_rwlock_writer_preference();
    test_rwlock_write_exit_non_owner_traps();
    return 0;
}
