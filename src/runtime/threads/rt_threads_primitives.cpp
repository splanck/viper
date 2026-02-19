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

/// @file
/// @brief Cross-platform implementations for Gate, Barrier, and RwLock.
/// @details Provides FIFO-fair gates, reusable barriers, and writer-preferred
///          reader-writer locks used by Viper.Threads.* runtime APIs.

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

/// @brief Per-thread waiter state for gate acquisition.
/// @details Each waiting thread owns a condition variable and a granted flag
///          that is set when a permit is reserved for it.
struct GateWaiter
{
    std::condition_variable cv;
    bool granted = false;
};

/// @brief Shared gate state implementing a FIFO semaphore.
/// @details Tracks permit count and a queue of waiters to ensure fairness.
struct GateState
{
    explicit GateState(int64_t initial_permits) : permits(initial_permits) {}

    std::mutex mu;
    int64_t permits = 0;
    std::deque<GateWaiter *> waiters;
};

/// @brief Runtime object wrapper storing gate state.
/// @details The RtGate object is managed by the runtime heap and owns a pointer
///          to the heap-allocated GateState.
struct RtGate
{
    GateState *state = nullptr;
};

//===----------------------------------------------------------------------===//
// Shared Validation Helper
//===----------------------------------------------------------------------===//

/// @brief Validate a threading primitive object and return its typed wrapper.
/// @details Generic validation for all threading primitives (Gate, Barrier, RwLock).
///          Traps with descriptive messages if the object or its internal state is null.
/// @tparam T The typed wrapper type (RtGate, RtBarrier, RtRwLock).
/// @param obj Opaque object pointer passed from the runtime.
/// @param typeName Name of the type for error messages (e.g., "Gate").
/// @param what Custom error message, or nullptr to use default.
/// @return Valid typed pointer, or nullptr if validation fails.
template <typename T> static T *requireObject(void *obj, const char *typeName, const char *what)
{
    if (!obj)
    {
        std::string msg = what ? what : (std::string(typeName) + ": null object");
        rt_trap(msg.c_str());
        return nullptr;
    }
    auto *typed = static_cast<T *>(obj);
    if (!typed->state)
    {
        std::string msg = std::string(typeName) + ": invalid object";
        rt_trap(msg.c_str());
        return nullptr;
    }
    return typed;
}

/// @brief Validate a gate object pointer and return its typed wrapper.
/// @param gate Opaque gate pointer passed from the runtime.
/// @param what Error string to use when trapping on NULL.
/// @return Valid RtGate pointer, or nullptr if validation fails.
static RtGate *require_gate(void *gate, const char *what)
{
    return requireObject<RtGate>(gate, "Gate", what);
}

/// @brief Finalizer invoked when a gate object is collected.
/// @details Releases the heap-allocated GateState and clears the pointer.
/// @param obj Runtime object pointer to finalize.
static void gate_finalizer(void *obj)
{
    auto *gate = static_cast<RtGate *>(obj);
    delete gate->state;
    gate->state = nullptr;
}

// ============================================================================
// Viper.Threads.Barrier
// ============================================================================

/// @brief Shared barrier state for coordinating fixed parties.
/// @details Tracks arrival count and generation so the barrier can be reused.
struct BarrierState
{
    explicit BarrierState(int64_t parties_) : parties(parties_) {}

    std::mutex mu;
    std::condition_variable cv;
    int64_t parties = 0;
    int64_t waiting = 0;
    int64_t generation = 0;
};

/// @brief Runtime object wrapper storing barrier state.
/// @details The RtBarrier object is managed by the runtime heap and owns a
///          pointer to the heap-allocated BarrierState.
struct RtBarrier
{
    BarrierState *state = nullptr;
};

/// @brief Validate a barrier object pointer and return its typed wrapper.
/// @param barrier Opaque barrier pointer passed from the runtime.
/// @param what Error string to use when trapping on NULL.
/// @return Valid RtBarrier pointer, or nullptr if validation fails.
static RtBarrier *require_barrier(void *barrier, const char *what)
{
    return requireObject<RtBarrier>(barrier, "Barrier", what);
}

/// @brief Finalizer invoked when a barrier object is collected.
/// @details Releases the heap-allocated BarrierState and clears the pointer.
/// @param obj Runtime object pointer to finalize.
static void barrier_finalizer(void *obj)
{
    auto *barrier = static_cast<RtBarrier *>(obj);
    delete barrier->state;
    barrier->state = nullptr;
}

// ============================================================================
// Viper.Threads.RwLock
// ============================================================================

/// @brief Writer wait node for the reader-writer lock.
/// @details Writers enqueue in FIFO order and are signaled individually.
struct RwLockWriterWaiter
{
    std::condition_variable cv;
};

/// @brief Shared reader-writer lock state with writer preference.
/// @details Tracks active readers, writer ownership, recursion depth, and a
///          FIFO queue of waiting writers.
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

/// @brief Runtime object wrapper storing reader-writer lock state.
/// @details The RtRwLock object is managed by the runtime heap and owns a
///          pointer to the heap-allocated RwLockState.
struct RtRwLock
{
    RwLockState *state = nullptr;
};

/// @brief Validate a reader-writer lock pointer and return its typed wrapper.
/// @param lock Opaque lock pointer passed from the runtime.
/// @param what Error string to use when trapping on NULL.
/// @return Valid RtRwLock pointer, or nullptr if validation fails.
static RtRwLock *require_rwlock(void *lock, const char *what)
{
    return requireObject<RtRwLock>(lock, "RwLock", what);
}

/// @brief Finalizer invoked when a reader-writer lock object is collected.
/// @details Releases the heap-allocated RwLockState and clears the pointer.
/// @param obj Runtime object pointer to finalize.
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

    /// @brief Create a new gate (counting semaphore) with initial permits.
    /// @details Permits must be non-negative; otherwise the runtime traps.
    ///          The returned object owns an internal GateState.
    /// @param permits Initial permit count.
    /// @return Opaque gate object, or NULL on allocation failure.
    void *rt_gate_new(int64_t permits)
    {
        if (permits < 0)
        {
            rt_trap("Gate.New: permits cannot be negative");
            return nullptr;
        }

        auto *gate = static_cast<RtGate *>(rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtGate)));
        if (!gate)
        {
            rt_trap("Gate.New: alloc failed");
            return nullptr; // Unreachable, but silences compiler warnings
        }

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

    /// @brief Enter the gate, blocking until a permit is available.
    /// @details If permits are available and no waiters exist, this function
    ///          consumes a permit and returns immediately. Otherwise it queues
    ///          the caller and waits until a permit is granted in FIFO order.
    /// @param gate Opaque gate object pointer.
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

    /// @brief Attempt to enter the gate without blocking.
    /// @details Succeeds only if there are no waiters and at least one permit.
    /// @param gate Opaque gate object pointer.
    /// @return 1 if a permit was acquired, 0 otherwise.
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

    /// @brief Attempt to enter the gate with a timeout.
    /// @details Waits up to @p ms milliseconds for a permit to be granted. A
    ///          timeout removes the waiter from the queue before returning.
    /// @param gate Opaque gate object pointer.
    /// @param ms Timeout in milliseconds (negative treated as zero).
    /// @return 1 if a permit was acquired, 0 on timeout.
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

    /// @brief Release a single permit back to the gate.
    /// @details Equivalent to @ref rt_gate_leave_many with a count of one.
    /// @param gate Opaque gate object pointer.
    void rt_gate_leave(void *gate)
    {
        rt_gate_leave_many(gate, 1);
    }

    /// @brief Release multiple permits back to the gate.
    /// @details Increments the permit count and wakes queued waiters in FIFO
    ///          order, reserving a permit for each woken thread.
    /// @param gate Opaque gate object pointer.
    /// @param count Number of permits to release (must be non-negative).
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

    /// @brief Query the current permit count.
    /// @details Returned under the gate's lock to ensure a consistent snapshot.
    /// @param gate Opaque gate object pointer.
    /// @return Current number of available permits.
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

    /// @brief Create a new reusable barrier.
    /// @details The barrier releases when @p parties threads have arrived. The
    ///          party count must be at least one.
    /// @param parties Number of participating threads.
    /// @return Opaque barrier object, or NULL on allocation failure.
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
        {
            rt_trap("Barrier.New: alloc failed");
            return nullptr; // Unreachable, but silences compiler warnings
        }

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

    /// @brief Arrive at the barrier and wait for all parties.
    /// @details Returns the arrival index for this generation (0-based). The
    ///          last arriving thread releases all waiters and advances the
    ///          generation so the barrier can be reused.
    /// @param barrier Opaque barrier object pointer.
    /// @return Arrival index in the current generation.
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

    /// @brief Reset the barrier to a new generation.
    /// @details Traps if any threads are currently waiting at the barrier to
    ///          avoid leaving them stranded.
    /// @param barrier Opaque barrier object pointer.
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

    /// @brief Get the configured party count for the barrier.
    /// @param barrier Opaque barrier object pointer.
    /// @return Number of parties required to release the barrier.
    int64_t rt_barrier_get_parties(void *barrier)
    {
        RtBarrier *b = require_barrier(barrier, "Barrier.get_Parties: null object");
        if (!b)
            return 0;

        BarrierState &state = *b->state;
        std::unique_lock<std::mutex> lock(state.mu);
        return state.parties;
    }

    /// @brief Get the number of parties currently waiting.
    /// @param barrier Opaque barrier object pointer.
    /// @return Count of threads waiting in the current generation.
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

    /// @brief Create a new reader-writer lock instance.
    /// @details The lock is writer-preferred to prevent writer starvation.
    /// @return Opaque reader-writer lock object, or NULL on failure.
    void *rt_rwlock_new(void)
    {
        auto *lock =
            static_cast<RtRwLock *>(rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtRwLock)));
        if (!lock)
        {
            rt_trap("RwLock.New: alloc failed");
            return nullptr; // Unreachable, but silences compiler warnings
        }

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

    /// @brief Acquire the lock in shared (reader) mode.
    /// @details Blocks while a writer is active or waiting to preserve writer
    ///          preference. Multiple readers may enter concurrently.
    /// @param lock Opaque reader-writer lock object pointer.
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

    /// @brief Release a previously acquired read lock.
    /// @details Traps if no matching read lock is held.
    /// @param lock Opaque reader-writer lock object pointer.
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

    /// @brief Acquire the lock in exclusive (writer) mode.
    /// @details Blocks until no readers or writers are active. If the calling
    ///          thread already owns the write lock, the recursion count is
    ///          incremented and the function returns immediately.
    /// @param lock Opaque reader-writer lock object pointer.
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

    /// @brief Release a previously acquired write lock.
    /// @details Traps if the caller is not the owner or if no write lock is
    ///          held. Writer recursion is decremented and the lock is released
    ///          when the recursion count reaches zero.
    /// @param lock Opaque reader-writer lock object pointer.
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

    /// @brief Attempt to acquire a read lock without blocking.
    /// @details Succeeds only if no writer is active and no writers are waiting.
    /// @param lock Opaque reader-writer lock object pointer.
    /// @return 1 if acquired, 0 otherwise.
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

    /// @brief Attempt to acquire a write lock without blocking.
    /// @details Succeeds only if no readers or writers are active. If the
    ///          caller already owns the write lock, recursion is incremented
    ///          and the call succeeds.
    /// @param lock Opaque reader-writer lock object pointer.
    /// @return 1 if acquired, 0 otherwise.
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

    /// @brief Query the number of active readers.
    /// @details Returned under the lock for a consistent snapshot.
    /// @param lock Opaque reader-writer lock object pointer.
    /// @return Number of active reader holders.
    int64_t rt_rwlock_get_readers(void *lock)
    {
        RtRwLock *rw = require_rwlock(lock, "RwLock.get_Readers: null object");
        if (!rw)
            return 0;

        RwLockState &state = *rw->state;
        std::unique_lock<std::mutex> lk(state.mu);
        return state.active_readers;
    }

    /// @brief Check whether a writer currently holds the lock.
    /// @details Returned under the lock for a consistent snapshot.
    /// @param lock Opaque reader-writer lock object pointer.
    /// @return 1 if a writer is active, 0 otherwise.
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
