//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_threads_primitives.cpp
// Purpose: Cross-platform threading primitives backing Viper.Threads.* classes.
// Key invariants:
//   - Gate acquisition is FIFO-fair across waiters.
//   - Barrier releases all parties simultaneously and resets per generation.
//   - RwLock provides writer-preference to prevent writer starvation.
// Ownership/Lifetime:
//   - Gate/Barrier/RwLock objects are runtime-managed (refcounted).
//   - Internal state is heap-allocated and freed via rt_obj finalizers.
//
// Notes:
//   - Implemented in C++ to remain cross-platform (Windows/macOS/Linux) without
//     additional dependencies beyond the C++ standard library.
//
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include "rt.hpp"
#include "rt_object.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <new>
#include <thread>

namespace
{

// ============================================================================
// Viper.Threads.Gate
// ============================================================================

struct GateWaiter
{
    std::condition_variable cv;
    bool granted = false;
};

struct GateState
{
    explicit GateState(int64_t initial_permits)
        : permits(initial_permits)
    {
    }

    std::mutex mu;
    int64_t permits = 0;
    std::deque<GateWaiter *> waiters;
};

struct RtGate
{
    GateState *state = nullptr;
};

static RtGate *require_gate(void *gate, const char *what)
{
    if (!gate)
    {
        rt_trap(what ? what : "Gate: null object");
        return nullptr;
    }
    auto *g = static_cast<RtGate *>(gate);
    if (!g->state)
    {
        rt_trap("Gate: invalid object");
        return nullptr;
    }
    return g;
}

static void gate_finalizer(void *obj)
{
    auto *gate = static_cast<RtGate *>(obj);
    delete gate->state;
    gate->state = nullptr;
}

// ============================================================================
// Viper.Threads.Barrier
// ============================================================================

struct BarrierState
{
    explicit BarrierState(int64_t parties_)
        : parties(parties_)
    {
    }

    std::mutex mu;
    std::condition_variable cv;
    int64_t parties = 0;
    int64_t waiting = 0;
    int64_t generation = 0;
};

struct RtBarrier
{
    BarrierState *state = nullptr;
};

static RtBarrier *require_barrier(void *barrier, const char *what)
{
    if (!barrier)
    {
        rt_trap(what ? what : "Barrier: null object");
        return nullptr;
    }
    auto *b = static_cast<RtBarrier *>(barrier);
    if (!b->state)
    {
        rt_trap("Barrier: invalid object");
        return nullptr;
    }
    return b;
}

static void barrier_finalizer(void *obj)
{
    auto *barrier = static_cast<RtBarrier *>(obj);
    delete barrier->state;
    barrier->state = nullptr;
}

// ============================================================================
// Viper.Threads.RwLock
// ============================================================================

struct RwLockWriterWaiter
{
    std::condition_variable cv;
};

struct RwLockState
{
    std::mutex mu;
    std::condition_variable readers_cv;

    int64_t active_readers = 0;

    bool writer_active = false;
    std::thread::id writer_owner;
    int64_t write_recursion = 0;

    std::deque<RwLockWriterWaiter *> waiting_writers;
};

struct RtRwLock
{
    RwLockState *state = nullptr;
};

static RtRwLock *require_rwlock(void *lock, const char *what)
{
    if (!lock)
    {
        rt_trap(what ? what : "RwLock: null object");
        return nullptr;
    }
    auto *rw = static_cast<RtRwLock *>(lock);
    if (!rw->state)
    {
        rt_trap("RwLock: invalid object");
        return nullptr;
    }
    return rw;
}

static void rwlock_finalizer(void *obj)
{
    auto *rw = static_cast<RtRwLock *>(obj);
    delete rw->state;
    rw->state = nullptr;
}

} // namespace

extern "C"
{

// ============================================================================
// Viper.Threads.Gate
// ============================================================================

void *rt_gate_new(int64_t permits)
{
    if (permits < 0)
    {
        rt_trap("Gate.New: permits cannot be negative");
        return nullptr;
    }

    auto *gate = static_cast<RtGate *>(rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtGate)));
    if (!gate)
        rt_trap("Gate.New: alloc failed");
    if (!gate)
        return nullptr;

    auto *state = new (std::nothrow) GateState(permits);
    if (!state)
    {
        rt_trap("Gate.New: alloc failed");
        return nullptr;
    }

    gate->state = state;
    rt_obj_set_finalizer(gate, &gate_finalizer);
    return gate;
}

void rt_gate_enter(void *gate)
{
    RtGate *g = require_gate(gate, "Gate.Enter: null object");
    if (!g)
        return;

    GateState &state = *g->state;
    std::unique_lock<std::mutex> lock(state.mu);

    if (state.waiters.empty() && state.permits > 0)
    {
        --state.permits;
        return;
    }

    GateWaiter waiter;
    state.waiters.push_back(&waiter);
    while (!waiter.granted)
    {
        waiter.cv.wait(lock);
    }
}

int8_t rt_gate_try_enter(void *gate)
{
    RtGate *g = require_gate(gate, "Gate.TryEnter: null object");
    if (!g)
        return 0;

    GateState &state = *g->state;
    std::unique_lock<std::mutex> lock(state.mu);
    if (!state.waiters.empty() || state.permits <= 0)
        return 0;
    --state.permits;
    return 1;
}

int8_t rt_gate_try_enter_for(void *gate, int64_t ms)
{
    RtGate *g = require_gate(gate, "Gate.TryEnterFor: null object");
    if (!g)
        return 0;

    if (ms < 0)
        ms = 0;

    GateState &state = *g->state;
    std::unique_lock<std::mutex> lock(state.mu);

    if (state.waiters.empty() && state.permits > 0)
    {
        --state.permits;
        return 1;
    }

    if (ms == 0)
        return 0;

    GateWaiter waiter;
    state.waiters.push_back(&waiter);

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(ms));

    while (!waiter.granted)
    {
        if (waiter.cv.wait_until(lock, deadline) == std::cv_status::timeout && !waiter.granted)
        {
            auto it = std::find(state.waiters.begin(), state.waiters.end(), &waiter);
            if (it != state.waiters.end())
                state.waiters.erase(it);
            return 0;
        }
    }

    return 1;
}

void rt_gate_leave(void *gate)
{
    rt_gate_leave_many(gate, 1);
}

void rt_gate_leave_many(void *gate, int64_t count)
{
    RtGate *g = require_gate(gate, "Gate.Leave: null object");
    if (!g)
        return;

    if (count < 0)
    {
        rt_trap("Gate.Leave: count cannot be negative");
        return;
    }

    GateState &state = *g->state;
    std::unique_lock<std::mutex> lock(state.mu);

    state.permits += count;
    while (state.permits > 0 && !state.waiters.empty())
    {
        GateWaiter *waiter = state.waiters.front();
        state.waiters.pop_front();
        --state.permits; // Reserve the permit for the woken waiter.
        waiter->granted = true;
        waiter->cv.notify_one();
    }
}

int64_t rt_gate_get_permits(void *gate)
{
    RtGate *g = require_gate(gate, "Gate.get_Permits: null object");
    if (!g)
        return 0;

    GateState &state = *g->state;
    std::unique_lock<std::mutex> lock(state.mu);
    return state.permits;
}

// ============================================================================
// Viper.Threads.Barrier
// ============================================================================

void *rt_barrier_new(int64_t parties)
{
    if (parties < 1)
    {
        rt_trap("Barrier.New: parties must be >= 1");
        return nullptr;
    }

    auto *barrier =
        static_cast<RtBarrier *>(rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtBarrier)));
    if (!barrier)
        rt_trap("Barrier.New: alloc failed");
    if (!barrier)
        return nullptr;

    auto *state = new (std::nothrow) BarrierState(parties);
    if (!state)
    {
        rt_trap("Barrier.New: alloc failed");
        return nullptr;
    }

    barrier->state = state;
    rt_obj_set_finalizer(barrier, &barrier_finalizer);
    return barrier;
}

int64_t rt_barrier_arrive(void *barrier)
{
    RtBarrier *b = require_barrier(barrier, "Barrier.Arrive: null object");
    if (!b)
        return 0;

    BarrierState &state = *b->state;
    std::unique_lock<std::mutex> lock(state.mu);

    const int64_t index = state.waiting;
    const int64_t gen = state.generation;
    ++state.waiting;

    if (state.waiting == state.parties)
    {
        state.waiting = 0;
        ++state.generation;
        state.cv.notify_all();
        return index;
    }

    while (state.generation == gen)
    {
        state.cv.wait(lock);
    }

    return index;
}

void rt_barrier_reset(void *barrier)
{
    RtBarrier *b = require_barrier(barrier, "Barrier.Reset: null object");
    if (!b)
        return;

    BarrierState &state = *b->state;
    std::unique_lock<std::mutex> lock(state.mu);
    if (state.waiting != 0)
    {
        lock.unlock(); // Release lock before trap to avoid deadlock if longjmp is used
        rt_trap("Barrier.Reset: threads are waiting");
        return;
    }
    ++state.generation;
}

int64_t rt_barrier_get_parties(void *barrier)
{
    RtBarrier *b = require_barrier(barrier, "Barrier.get_Parties: null object");
    if (!b)
        return 0;

    BarrierState &state = *b->state;
    std::unique_lock<std::mutex> lock(state.mu);
    return state.parties;
}

int64_t rt_barrier_get_waiting(void *barrier)
{
    RtBarrier *b = require_barrier(barrier, "Barrier.get_Waiting: null object");
    if (!b)
        return 0;

    BarrierState &state = *b->state;
    std::unique_lock<std::mutex> lock(state.mu);
    return state.waiting;
}

// ============================================================================
// Viper.Threads.RwLock
// ============================================================================

void *rt_rwlock_new(void)
{
    auto *lock = static_cast<RtRwLock *>(rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtRwLock)));
    if (!lock)
        rt_trap("RwLock.New: alloc failed");
    if (!lock)
        return nullptr;

    auto *state = new (std::nothrow) RwLockState();
    if (!state)
    {
        rt_trap("RwLock.New: alloc failed");
        return nullptr;
    }

    lock->state = state;
    rt_obj_set_finalizer(lock, &rwlock_finalizer);
    return lock;
}

void rt_rwlock_read_enter(void *lock)
{
    RtRwLock *rw = require_rwlock(lock, "RwLock.ReadEnter: null object");
    if (!rw)
        return;

    RwLockState &state = *rw->state;
    std::unique_lock<std::mutex> lk(state.mu);
    while (state.writer_active || !state.waiting_writers.empty())
    {
        state.readers_cv.wait(lk);
    }
    ++state.active_readers;
}

void rt_rwlock_read_exit(void *lock)
{
    RtRwLock *rw = require_rwlock(lock, "RwLock.ReadExit: null object");
    if (!rw)
        return;

    RwLockState &state = *rw->state;
    std::unique_lock<std::mutex> lk(state.mu);
    if (state.active_readers <= 0)
    {
        lk.unlock(); // Release lock before trap to avoid deadlock if longjmp is used
        rt_trap("RwLock.ReadExit: exit without matching enter");
        return;
    }
    --state.active_readers;
    if (state.active_readers == 0 && !state.writer_active && !state.waiting_writers.empty())
    {
        state.waiting_writers.front()->cv.notify_one();
    }
}

void rt_rwlock_write_enter(void *lock)
{
    RtRwLock *rw = require_rwlock(lock, "RwLock.WriteEnter: null object");
    if (!rw)
        return;

    RwLockState &state = *rw->state;
    const std::thread::id tid = std::this_thread::get_id();
    std::unique_lock<std::mutex> lk(state.mu);

    if (state.writer_active && state.writer_owner == tid)
    {
        ++state.write_recursion;
        return;
    }

    RwLockWriterWaiter waiter;
    state.waiting_writers.push_back(&waiter);

    while (true)
    {
        const bool is_front = state.waiting_writers.front() == &waiter;
        if (is_front && !state.writer_active && state.active_readers == 0)
        {
            state.waiting_writers.pop_front();
            state.writer_active = true;
            state.writer_owner = tid;
            state.write_recursion = 1;
            return;
        }
        waiter.cv.wait(lk);
    }
}

void rt_rwlock_write_exit(void *lock)
{
    RtRwLock *rw = require_rwlock(lock, "RwLock.WriteExit: null object");
    if (!rw)
        return;

    RwLockState &state = *rw->state;
    const std::thread::id tid = std::this_thread::get_id();
    std::unique_lock<std::mutex> lk(state.mu);

    if (!state.writer_active)
    {
        lk.unlock(); // Release lock before trap to avoid deadlock if longjmp is used
        rt_trap("RwLock.WriteExit: exit without matching enter");
        return;
    }
    if (state.writer_owner != tid)
    {
        lk.unlock(); // Release lock before trap to avoid deadlock if longjmp is used
        rt_trap("RwLock.WriteExit: not owner");
        return;
    }

    --state.write_recursion;
    if (state.write_recursion > 0)
        return;

    state.writer_active = false;
    state.writer_owner = std::thread::id();

    if (!state.waiting_writers.empty())
    {
        state.waiting_writers.front()->cv.notify_one();
    }
    else
    {
        state.readers_cv.notify_all();
    }
}

int8_t rt_rwlock_try_read_enter(void *lock)
{
    RtRwLock *rw = require_rwlock(lock, "RwLock.TryReadEnter: null object");
    if (!rw)
        return 0;

    RwLockState &state = *rw->state;
    std::unique_lock<std::mutex> lk(state.mu);
    if (state.writer_active || !state.waiting_writers.empty())
        return 0;
    ++state.active_readers;
    return 1;
}

int8_t rt_rwlock_try_write_enter(void *lock)
{
    RtRwLock *rw = require_rwlock(lock, "RwLock.TryWriteEnter: null object");
    if (!rw)
        return 0;

    RwLockState &state = *rw->state;
    const std::thread::id tid = std::this_thread::get_id();
    std::unique_lock<std::mutex> lk(state.mu);

    if (state.writer_active && state.writer_owner == tid)
    {
        ++state.write_recursion;
        return 1;
    }

    if (state.writer_active || state.active_readers > 0 || !state.waiting_writers.empty())
        return 0;

    state.writer_active = true;
    state.writer_owner = tid;
    state.write_recursion = 1;
    return 1;
}

int64_t rt_rwlock_get_readers(void *lock)
{
    RtRwLock *rw = require_rwlock(lock, "RwLock.get_Readers: null object");
    if (!rw)
        return 0;

    RwLockState &state = *rw->state;
    std::unique_lock<std::mutex> lk(state.mu);
    return state.active_readers;
}

int8_t rt_rwlock_get_is_write_locked(void *lock)
{
    RtRwLock *rw = require_rwlock(lock, "RwLock.get_IsWriteLocked: null object");
    if (!rw)
        return 0;

    RwLockState &state = *rw->state;
    std::unique_lock<std::mutex> lk(state.mu);
    return state.writer_active ? 1 : 0;
}

} // extern "C"
