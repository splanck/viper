//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_threads.c
// Purpose: Implements OS thread creation and lifecycle management for the
//          Viper.Threads.Thread class. Abstracts pthreads (Unix/macOS) and
//          Win32 CreateThread. Supports Start, Join, TryJoin, JoinFor, IsAlive,
//          GetId, Sleep, and Yield.
//
// Key invariants:
//   - Thread IDs are unique, monotonically increasing, and never reused.
//   - A running thread holds a self-reference to prevent premature GC.
//   - The self-reference is released when the entry function returns.
//   - A thread cannot join itself; attempting to do so traps.
//   - Multiple threads may wait on the same thread via Join; all are notified.
//   - New threads inherit the RtContext from their parent.
//
// Ownership/Lifetime:
//   - Thread objects are heap-allocated and GC-managed.
//   - The running thread holds a retained self-reference for its lifetime.
//   - The entry function argument is not retained; callers own its lifetime.
//
// Links: src/runtime/threads/rt_threads.h (public API),
//        src/runtime/threads/rt_monitor.h (synchronization primitives),
//        src/runtime/rt_context.h (RtContext inheritance)
//
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include "rt_context.h"
#include "rt_internal.h"
#include "viper/runtime/rt.h"

#include "rt_object.h"

#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

//===----------------------------------------------------------------------===//
// Windows Threading Implementation
//===----------------------------------------------------------------------===//
//
// Uses Windows synchronization primitives:
// - CRITICAL_SECTION for mutex (faster than SRWLOCK for this use case)
// - CONDITION_VARIABLE for signaling between threads
// - CreateThread/CloseHandle for thread management
//
// Thread lifecycle on Windows:
// 1. CreateThread spawns the OS thread
// 2. Thread runs entry function via trampoline
// 3. On completion, signals waiters via CONDITION_VARIABLE
// 4. CloseHandle is called when thread object is finalized
//
//===----------------------------------------------------------------------===//

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/// @brief Function pointer type for thread entry functions.
typedef void (*rt_thread_entry_fn)(void *);

/// @brief Internal representation of a Viper thread (Windows).
///
/// Uses Windows CRITICAL_SECTION and CONDITION_VARIABLE for synchronization.
/// The thread handle is stored for potential future use (e.g., priority changes)
/// but is not required for join operations since we use condition variables.
typedef struct RtThread
{
    CRITICAL_SECTION cs;      ///< Critical section protecting state access.
    CONDITION_VARIABLE cv;    ///< Condition var for Join() signaling.
    HANDLE hThread;           ///< OS thread handle.
    DWORD threadId;           ///< OS thread ID for self-join detection.
    int finished;             ///< 1 when thread has completed.
    int joined;               ///< 1 after Join() called.
    int64_t id;               ///< Unique Viper thread identifier.
    RtContext *inherited_ctx; ///< Parent's runtime context.
    rt_thread_entry_fn entry; ///< User's entry function.
    void *arg;                ///< Argument to entry function.
} RtThread;

/// @brief Global counter for assigning unique thread IDs.
static volatile LONG64 g_next_thread_id_win = 1;

/// @brief Atomically generates the next unique thread ID.
static int64_t next_thread_id_win(void)
{
    return (int64_t)InterlockedIncrement64(&g_next_thread_id_win) - 1;
}

/// @brief Finalizer for RtThread objects, called during garbage collection.
static void rt_thread_finalize_win(void *obj)
{
    if (!obj)
        return;
    RtThread *t = (RtThread *)obj;
    if (t->hThread)
        CloseHandle(t->hThread);
    DeleteCriticalSection(&t->cs);
    // CONDITION_VARIABLE doesn't need explicit cleanup on Windows
}

/// @brief Thread trampoline that sets up context and runs the entry function.
static DWORD WINAPI rt_thread_trampoline_win(LPVOID p)
{
    RtThread *t = (RtThread *)p;
    if (t && t->inherited_ctx)
        rt_set_current_context(t->inherited_ctx);
    if (t && t->entry)
        t->entry(t->arg);
    rt_set_current_context(NULL);

    if (t)
    {
        EnterCriticalSection(&t->cs);
        t->finished = 1;
        WakeAllConditionVariable(&t->cv);
        LeaveCriticalSection(&t->cs);

        if (rt_obj_release_check0(t))
            rt_obj_free(t);
    }
    return 0;
}

/// @brief Validates a thread pointer and traps if NULL.
static RtThread *require_thread_win(void *thread, const char *what)
{
    if (!thread)
    {
        rt_trap(what ? what : "Thread: null thread");
        return NULL;
    }
    return (RtThread *)thread;
}

void *rt_thread_start(void *entry, void *arg)
{
    if (!entry)
        rt_trap("Thread.Start: null entry");
    if (!entry)
        return NULL;

    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();

    RtThread *t = (RtThread *)rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtThread));
    if (!t)
        rt_trap("Thread.Start: failed to create thread");
    if (!t)
        return NULL;

    InitializeCriticalSection(&t->cs);
    InitializeConditionVariable(&t->cv);

    t->hThread = NULL;
    t->threadId = 0;
    t->finished = 0;
    t->joined = 0;
    t->id = next_thread_id_win();
    t->inherited_ctx = ctx;
    t->entry = (rt_thread_entry_fn)entry;
    t->arg = arg;

    rt_obj_set_finalizer(t, rt_thread_finalize_win);

    // Hold a self-reference until the thread exits.
    rt_obj_retain_maybe(t);

    t->hThread = CreateThread(NULL, 0, rt_thread_trampoline_win, t, 0, &t->threadId);
    if (!t->hThread)
    {
        // Drop thread self-reference and the caller-visible reference, then trap.
        if (rt_obj_release_check0(t))
            rt_obj_free(t);
        if (rt_obj_release_check0(t))
            rt_obj_free(t);
        rt_trap("Thread.Start: failed to create thread");
        return NULL;
    }

    return t;
}

void rt_thread_join(void *thread)
{
    RtThread *t = require_thread_win(thread, "Thread.Join: null thread");
    if (!t)
        return;
    if (GetCurrentThreadId() == t->threadId)
    {
        rt_trap("Thread.Join: cannot join self");
        return;
    }

    EnterCriticalSection(&t->cs);
    if (t->joined)
    {
        LeaveCriticalSection(&t->cs);
        rt_trap("Thread.Join: already joined");
        return;
    }
    while (!t->finished)
    {
        SleepConditionVariableCS(&t->cv, &t->cs, INFINITE);
    }
    t->joined = 1;
    LeaveCriticalSection(&t->cs);
}

int8_t rt_thread_try_join(void *thread)
{
    RtThread *t = require_thread_win(thread, "Thread.TryJoin: null thread");
    if (!t)
        return 0;
    if (GetCurrentThreadId() == t->threadId)
    {
        rt_trap("Thread.Join: cannot join self");
        return 0;
    }

    EnterCriticalSection(&t->cs);
    if (t->joined)
    {
        LeaveCriticalSection(&t->cs);
        rt_trap("Thread.Join: already joined");
        return 0;
    }
    if (!t->finished)
    {
        LeaveCriticalSection(&t->cs);
        return 0;
    }
    t->joined = 1;
    LeaveCriticalSection(&t->cs);
    return 1;
}

int8_t rt_thread_join_for(void *thread, int64_t ms)
{
    RtThread *t = require_thread_win(thread, "Thread.JoinFor: null thread");
    if (!t)
        return 0;
    if (GetCurrentThreadId() == t->threadId)
    {
        rt_trap("Thread.Join: cannot join self");
        return 0;
    }

    if (ms < 0)
    {
        rt_thread_join(t);
        return 1;
    }

    EnterCriticalSection(&t->cs);
    if (t->joined)
    {
        LeaveCriticalSection(&t->cs);
        rt_trap("Thread.Join: already joined");
        return 0;
    }
    if (ms == 0)
    {
        if (!t->finished)
        {
            LeaveCriticalSection(&t->cs);
            return 0;
        }
        t->joined = 1;
        LeaveCriticalSection(&t->cs);
        return 1;
    }

    // Wait with timeout - use a loop to handle spurious wakes
    DWORD timeout_ms = (ms > MAXDWORD) ? MAXDWORD : (DWORD)ms;
    ULONGLONG start = GetTickCount64();
    ULONGLONG elapsed = 0;

    while (!t->finished)
    {
        DWORD remaining = (elapsed >= timeout_ms) ? 0 : (DWORD)(timeout_ms - elapsed);
        if (remaining == 0)
        {
            LeaveCriticalSection(&t->cs);
            return 0;
        }

        BOOL ok = SleepConditionVariableCS(&t->cv, &t->cs, remaining);
        if (!ok && GetLastError() == ERROR_TIMEOUT)
        {
            if (!t->finished)
            {
                LeaveCriticalSection(&t->cs);
                return 0;
            }
        }
        elapsed = GetTickCount64() - start;
    }

    t->joined = 1;
    LeaveCriticalSection(&t->cs);
    return 1;
}

int64_t rt_thread_get_id(void *thread)
{
    RtThread *t = require_thread_win(thread, "Thread.get_Id: null thread");
    if (!t)
        return 0;
    return t->id;
}

int8_t rt_thread_get_is_alive(void *thread)
{
    RtThread *t = require_thread_win(thread, "Thread.get_IsAlive: null thread");
    if (!t)
        return 0;
    EnterCriticalSection(&t->cs);
    const int alive = t->finished ? 0 : 1;
    LeaveCriticalSection(&t->cs);
    return (int8_t)alive;
}

void rt_thread_sleep(int64_t ms)
{
    if (ms < 0)
        ms = 0;
    if (ms > INT32_MAX)
        ms = INT32_MAX;
    rt_sleep_ms((int32_t)ms);
}

void rt_thread_yield(void)
{
    SwitchToThread();
}

#else

#include <pthread.h>
#include <sched.h>
#include <time.h>

/// @brief Function pointer type for thread entry functions.
typedef void (*rt_thread_entry_fn)(void *);

/// @brief Internal representation of a Viper thread.
///
/// This structure holds all state for a single thread, including synchronization
/// primitives for joining, the pthread handle, and thread metadata. The struct
/// is allocated as a GC-managed object via rt_obj_new_i64.
///
/// **State transitions:**
/// ```
/// Created ──Start()──▶ Running ──Entry returns──▶ Finished
///                                                    │
///                                         ──Join()──▶ Joined
/// ```
///
/// **Memory layout:**
/// ```
/// RtThread:
/// ┌────────────────────┬────────────────────────────────────────┐
/// │ mu                 │ Mutex protecting finished/joined flags │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ cv                 │ Condition variable for Join() waiting  │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ pthread            │ OS thread handle                       │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ finished           │ 1 when entry function has returned     │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ joined             │ 1 after successful Join() call         │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ id                 │ Unique thread ID (1, 2, 3, ...)        │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ inherited_ctx      │ Parent's RtContext (shared)            │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ entry              │ User's thread function                 │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ arg                │ Argument passed to entry function      │
/// └────────────────────┴────────────────────────────────────────┘
/// ```
typedef struct RtThread
{
    pthread_mutex_t mu;       ///< Mutex protecting state access.
    pthread_cond_t cv;        ///< Condition var for Join() signaling.
    pthread_t pthread;        ///< OS thread handle.
    int finished;             ///< 1 when thread has completed.
    int joined;               ///< 1 after Join() called.
    int64_t id;               ///< Unique thread identifier.
    RtContext *inherited_ctx; ///< Parent's runtime context.
    rt_thread_entry_fn entry; ///< User's entry function.
    void *arg;                ///< Argument to entry function.
} RtThread;

/// @brief Global counter for assigning unique thread IDs.
///
/// Thread IDs are assigned starting from 1 and increment atomically for each
/// new thread. IDs are never reused, even after threads complete.
static int64_t g_next_thread_id = 1;

/// @brief Atomically generates the next unique thread ID.
///
/// @return A unique thread ID (1, 2, 3, ...).
///
/// @note Thread-safe via atomic fetch-and-add operation.
static int64_t next_thread_id(void)
{
    return (int64_t)__atomic_fetch_add(&g_next_thread_id, 1, __ATOMIC_RELAXED);
}

/// @brief Finalizer for RtThread objects, called during garbage collection.
///
/// Cleans up the mutex and condition variable allocated during thread creation.
/// The pthread handle itself doesn't need cleanup since we detach threads.
///
/// @param obj Pointer to the RtThread object. May be NULL (no-op).
static void rt_thread_finalize(void *obj)
{
    if (!obj)
        return;
    RtThread *t = (RtThread *)obj;
    (void)pthread_mutex_destroy(&t->mu);
    (void)pthread_cond_destroy(&t->cv);
}

static void *rt_thread_trampoline(void *p)
{
    RtThread *t = (RtThread *)p;
    if (t && t->inherited_ctx)
        rt_set_current_context(t->inherited_ctx);
    if (t && t->entry)
        t->entry(t->arg);
    rt_set_current_context(NULL);

    if (t)
    {
        pthread_mutex_lock(&t->mu);
        t->finished = 1;
        pthread_cond_broadcast(&t->cv);
        pthread_mutex_unlock(&t->mu);

        if (rt_obj_release_check0(t))
            rt_obj_free(t);
    }
    return NULL;
}

/// @brief Validates a thread pointer and traps if NULL.
///
/// Helper function used by Join, TryJoin, JoinFor, and property accessors
/// to validate the thread argument before proceeding.
///
/// @param thread Pointer to the thread object to validate.
/// @param what Error message context (e.g., "Thread.Join: null thread").
///
/// @return The validated RtThread pointer, or NULL after trapping.
static RtThread *require_thread(void *thread, const char *what)
{
    if (!thread)
    {
        rt_trap(what ? what : "Thread: null thread");
        return NULL;
    }
    return (RtThread *)thread;
}

/// @brief Calculates an absolute timespec for a deadline ms in the future.
///
/// Used by JoinFor to compute the deadline for pthread_cond_timedwait.
///
/// @param ms Milliseconds from now for the deadline. If <= 0, returns current time.
///
/// @return Absolute timespec representing (now + ms milliseconds).
static struct timespec abs_time_ms_from_now(int64_t ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (ms <= 0)
        return ts;

    const int64_t kNsPerMs = 1000000;
    int64_t add_ns = ms * kNsPerMs;
    int64_t sec = (int64_t)ts.tv_sec + add_ns / 1000000000;
    int64_t ns = (int64_t)ts.tv_nsec + add_ns % 1000000000;
    if (ns >= 1000000000)
    {
        sec += 1;
        ns -= 1000000000;
    }
    ts.tv_sec = (time_t)sec;
    ts.tv_nsec = (long)ns;
    return ts;
}

/// @brief Creates and starts a new thread.
///
/// Spawns a new OS thread that executes the given entry function with the
/// provided argument. The new thread inherits the runtime context from the
/// calling thread, including RNG state and command-line arguments.
///
/// **Example:**
/// ```
/// ' Start a thread with a simple function
/// Dim t = Thread.Start(AddressOf Worker, data)
///
/// ' Start with a lambda (simplified)
/// Dim t = Thread.Start(Sub() DoWork())
/// ```
///
/// **Thread lifecycle after Start:**
/// 1. Thread object is created and initialized
/// 2. OS thread is spawned via pthread_create
/// 3. New thread begins executing entry function
/// 4. Thread detaches (OS resources freed when finished)
/// 5. Entry function runs to completion
/// 6. Thread marks itself as finished and signals waiters
///
/// @param entry Function pointer to the thread entry point. Must not be NULL.
///              Signature: void entry(void *arg)
/// @param arg Argument to pass to the entry function. May be NULL.
///
/// @return A Thread object that can be used for joining, or NULL if creation
///         failed after trapping.
///
/// @note Traps if entry is NULL.
/// @note Traps if thread creation fails (resource exhaustion, etc.).
/// @note The thread is detached immediately - no need to manually cleanup.
/// @note Thread holds a self-reference until it finishes.
///
/// @see rt_thread_join For waiting for thread completion
/// @see rt_thread_get_id For getting the thread's unique ID
void *rt_thread_start(void *entry, void *arg)
{
    if (!entry)
        rt_trap("Thread.Start: null entry");
    if (!entry)
        return NULL;

    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();

    RtThread *t = (RtThread *)rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(RtThread));
    if (!t)
        rt_trap("Thread.Start: failed to create thread");
    if (!t)
        return NULL;

    (void)pthread_mutex_init(&t->mu, NULL);
    (void)pthread_cond_init(&t->cv, NULL);

    t->finished = 0;
    t->joined = 0;
    t->id = next_thread_id();
    t->inherited_ctx = ctx;
    t->entry = (rt_thread_entry_fn)entry;
    t->arg = arg;

    rt_obj_set_finalizer(t, rt_thread_finalize);

    // Hold a self-reference until the thread exits.
    rt_obj_retain_maybe(t);

    if (pthread_create(&t->pthread, NULL, rt_thread_trampoline, t) != 0)
    {
        // Drop thread self-reference and the caller-visible reference, then trap.
        if (rt_obj_release_check0(t))
            rt_obj_free(t);
        if (rt_obj_release_check0(t))
            rt_obj_free(t);
        rt_trap("Thread.Start: failed to create thread");
        return NULL;
    }

    // Detach so OS resources are reclaimed even if the thread is never joined.
    (void)pthread_detach(t->pthread);
    return t;
}

/// @brief Waits indefinitely for a thread to complete.
///
/// Blocks the calling thread until the specified thread finishes executing
/// its entry function. If the thread has already finished, returns immediately.
///
/// **Example:**
/// ```
/// Dim worker = Thread.Start(Sub() DoLongTask())
/// ' ... do other work ...
/// worker.Join()  ' Wait for worker to finish
/// Print "Worker completed"
/// ```
///
/// **Error conditions:**
/// - Traps if thread is NULL
/// - Traps if thread was already joined (each thread can only be joined once)
/// - Traps if a thread tries to join itself (deadlock prevention)
///
/// @param thread The Thread object to wait for. Must not be NULL.
///
/// @note Blocks until the thread finishes - use JoinFor for timeouts.
/// @note Each thread can only be joined once.
/// @note A thread cannot join itself (would deadlock).
/// @note Multiple threads can wait on the same thread - all will be notified.
///
/// @see rt_thread_try_join For non-blocking check
/// @see rt_thread_join_for For waiting with timeout
void rt_thread_join(void *thread)
{
    RtThread *t = require_thread(thread, "Thread.Join: null thread");
    if (!t)
        return;
    if (pthread_equal(pthread_self(), t->pthread))
    {
        rt_trap("Thread.Join: cannot join self");
        return;
    }

    pthread_mutex_lock(&t->mu);
    if (t->joined)
    {
        pthread_mutex_unlock(&t->mu);
        rt_trap("Thread.Join: already joined");
        return;
    }
    while (!t->finished)
    {
        (void)pthread_cond_wait(&t->cv, &t->mu);
    }
    t->joined = 1;
    pthread_mutex_unlock(&t->mu);
}

/// @brief Non-blocking attempt to join a thread.
///
/// Checks if the thread has finished and joins it if so. Unlike Join(), this
/// never blocks - it returns immediately with the result.
///
/// **Example:**
/// ```
/// Dim worker = Thread.Start(Sub() DoWork())
///
/// ' Poll for completion
/// While Not worker.TryJoin()
///     DoOtherWork()
///     Sleep(100)
/// Wend
/// Print "Worker done"
/// ```
///
/// @param thread The Thread object to check. Must not be NULL.
///
/// @return 1 (true) if the thread was finished and is now joined.
///         0 (false) if the thread is still running.
///
/// @note Traps if thread is NULL.
/// @note Traps if thread was already joined.
/// @note Traps if called on self.
/// @note Once this returns true, the thread is considered joined.
///
/// @see rt_thread_join For blocking wait
/// @see rt_thread_join_for For waiting with timeout
int8_t rt_thread_try_join(void *thread)
{
    RtThread *t = require_thread(thread, "Thread.TryJoin: null thread");
    if (!t)
        return 0;
    if (pthread_equal(pthread_self(), t->pthread))
    {
        rt_trap("Thread.Join: cannot join self");
        return 0;
    }

    pthread_mutex_lock(&t->mu);
    if (t->joined)
    {
        pthread_mutex_unlock(&t->mu);
        rt_trap("Thread.Join: already joined");
        return 0;
    }
    if (!t->finished)
    {
        pthread_mutex_unlock(&t->mu);
        return 0;
    }
    t->joined = 1;
    pthread_mutex_unlock(&t->mu);
    return 1;
}

/// @brief Waits for a thread to complete with a timeout.
///
/// Blocks until the thread finishes or the specified timeout elapses, whichever
/// comes first. This is useful when you want to wait for a thread but also need
/// to handle the case where it takes too long.
///
/// **Example:**
/// ```
/// Dim worker = Thread.Start(Sub() DoWork())
///
/// ' Wait up to 5 seconds
/// If worker.JoinFor(5000) Then
///     Print "Worker finished in time"
/// Else
///     Print "Worker still running after 5 seconds"
///     ' Can continue waiting, cancel, etc.
/// End If
/// ```
///
/// **Timeout behavior:**
/// | ms value | Behavior                                      |
/// |----------|-----------------------------------------------|
/// | < 0      | Wait indefinitely (same as Join())            |
/// | = 0      | Check immediately (same as TryJoin())         |
/// | > 0      | Wait up to ms milliseconds                    |
///
/// @param thread The Thread object to wait for. Must not be NULL.
/// @param ms Maximum time to wait in milliseconds.
///
/// @return 1 (true) if the thread finished and was joined.
///         0 (false) if the timeout elapsed before the thread finished.
///
/// @note Traps if thread is NULL.
/// @note Traps if thread was already joined.
/// @note Traps if called on self.
/// @note If timeout occurs, the thread is NOT joined and can be waited on again.
///
/// @see rt_thread_join For indefinite waiting
/// @see rt_thread_try_join For immediate check
int8_t rt_thread_join_for(void *thread, int64_t ms)
{
    RtThread *t = require_thread(thread, "Thread.JoinFor: null thread");
    if (!t)
        return 0;
    if (pthread_equal(pthread_self(), t->pthread))
    {
        rt_trap("Thread.Join: cannot join self");
        return 0;
    }

    if (ms < 0)
    {
        rt_thread_join(t);
        return 1;
    }

    pthread_mutex_lock(&t->mu);
    if (t->joined)
    {
        pthread_mutex_unlock(&t->mu);
        rt_trap("Thread.Join: already joined");
        return 0;
    }
    if (ms == 0)
    {
        if (!t->finished)
        {
            pthread_mutex_unlock(&t->mu);
            return 0;
        }
        t->joined = 1;
        pthread_mutex_unlock(&t->mu);
        return 1;
    }

    const struct timespec deadline = abs_time_ms_from_now(ms);
    while (!t->finished)
    {
        const int rc = pthread_cond_timedwait(&t->cv, &t->mu, &deadline);
        if (rc == ETIMEDOUT && !t->finished)
        {
            pthread_mutex_unlock(&t->mu);
            return 0;
        }
    }

    t->joined = 1;
    pthread_mutex_unlock(&t->mu);
    return 1;
}

/// @brief Gets the unique ID of a thread.
///
/// Returns the thread's unique identifier, which was assigned when the thread
/// was created. Thread IDs are sequential starting from 1 and are never reused.
///
/// **Example:**
/// ```
/// Dim t = Thread.Start(Sub() DoWork())
/// Print "Started thread " & t.Id
/// ```
///
/// @param thread The Thread object to query. Must not be NULL.
///
/// @return The thread's unique ID (1, 2, 3, ...), or 0 if thread is NULL.
///
/// @note Traps if thread is NULL.
/// @note Thread IDs are stable - they don't change after thread creation.
int64_t rt_thread_get_id(void *thread)
{
    RtThread *t = require_thread(thread, "Thread.get_Id: null thread");
    if (!t)
        return 0;
    return t->id;
}

/// @brief Checks if a thread is still running.
///
/// Returns true if the thread's entry function is still executing, false if
/// the thread has completed. This is a non-blocking query of the thread's state.
///
/// **Example:**
/// ```
/// Dim t = Thread.Start(Sub() DoWork())
///
/// While t.IsAlive
///     Print "Thread still working..."
///     Sleep(500)
/// Wend
/// Print "Thread done"
/// ```
///
/// @param thread The Thread object to query. Must not be NULL.
///
/// @return 1 (true) if the thread is still running.
///         0 (false) if the thread has finished.
///
/// @note Traps if thread is NULL.
/// @note Thread-safe - uses mutex to access state.
/// @note A finished thread may not be joined yet; IsAlive checks execution state.
int8_t rt_thread_get_is_alive(void *thread)
{
    RtThread *t = require_thread(thread, "Thread.get_IsAlive: null thread");
    if (!t)
        return 0;
    pthread_mutex_lock(&t->mu);
    const int alive = t->finished ? 0 : 1;
    pthread_mutex_unlock(&t->mu);
    return (int8_t)alive;
}

/// @brief Suspends the calling thread for the specified duration.
///
/// Puts the current thread to sleep for approximately the specified number of
/// milliseconds. Other threads continue to run during this time.
///
/// **Example:**
/// ```
/// ' Pause for 1 second
/// Thread.Sleep(1000)
///
/// ' Brief yield (still gives up time slice)
/// Thread.Sleep(0)
/// ```
///
/// @param ms Duration to sleep in milliseconds. Clamped to [0, INT32_MAX].
///
/// @note Values less than 0 are treated as 0.
/// @note Actual sleep time may be longer due to OS scheduling.
/// @note Uses rt_sleep_ms internally.
///
/// @see rt_thread_yield For giving up time slice without sleeping
/// @see rt_sleep_ms For the underlying implementation
void rt_thread_sleep(int64_t ms)
{
    if (ms < 0)
        ms = 0;
    if (ms > INT32_MAX)
        ms = INT32_MAX;
    rt_sleep_ms((int32_t)ms);
}

/// @brief Yields the current thread's time slice to other threads.
///
/// Voluntarily gives up the current thread's CPU time, allowing other threads
/// (including other Viper threads and system threads) to run. The thread
/// becomes immediately eligible to run again.
///
/// **Use cases:**
/// - Busy-wait loops (spin locks) to avoid hogging CPU
/// - Cooperative multitasking scenarios
/// - Allowing background threads to make progress
///
/// **Example:**
/// ```
/// ' Busy wait with yielding
/// While Not dataReady
///     Thread.Yield()  ' Let other threads run
/// Wend
/// ```
///
/// @note The thread may run again immediately if no other threads are ready.
/// @note Uses sched_yield() on Unix systems.
/// @note Lighter weight than Sleep(0).
void rt_thread_yield(void)
{
    (void)sched_yield();
}

#endif

//===----------------------------------------------------------------------===//
// Safe Thread Implementation (platform-independent)
//===----------------------------------------------------------------------===//

/// @brief Function pointer type for safe thread entry (matches rt_thread_entry_fn).
typedef void (*rt_safe_entry_fn)(void *);

/// @brief Context for a safe thread that captures trap errors instead of
///        terminating the process.
typedef struct SafeThreadCtx
{
    rt_safe_entry_fn entry;
    void *arg;
    void *thread;    // The underlying thread handle from rt_thread_start
    int8_t trapped;  // 1 if the thread exited due to a trap
    char error[512]; // Captured trap error message
} SafeThreadCtx;

/// @brief Entry point wrapper that sets up trap recovery.
static void safe_thread_entry(void *ctx_ptr)
{
    SafeThreadCtx *ctx = (SafeThreadCtx *)ctx_ptr;
    if (!ctx || !ctx->entry)
        return;

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);

    if (setjmp(recovery) == 0)
    {
        ctx->entry(ctx->arg);
    }
    else
    {
        ctx->trapped = 1;
        const char *err = rt_trap_get_error();
        snprintf(ctx->error, sizeof(ctx->error), "%s", err ? err : "Unknown trap");
    }

    rt_trap_clear_recovery();
}

void *rt_thread_start_safe(void *entry, void *arg)
{
    if (!entry)
        rt_trap("Thread.StartSafe: null entry");
    if (!entry)
        return NULL;

    SafeThreadCtx *ctx =
        (SafeThreadCtx *)rt_obj_new_i64(/*class_id=*/0, (int64_t)sizeof(SafeThreadCtx));
    if (!ctx)
        rt_trap("Thread.StartSafe: failed to allocate context");
    if (!ctx)
        return NULL;

    ctx->entry = (rt_safe_entry_fn)entry;
    ctx->arg = arg;
    ctx->trapped = 0;
    ctx->error[0] = '\0';

    ctx->thread = rt_thread_start((void *)safe_thread_entry, ctx);
    return ctx;
}

int8_t rt_thread_has_error(void *obj)
{
    if (!obj)
        return 0;
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    return ctx->trapped;
}

rt_string rt_thread_get_error(void *obj)
{
    if (!obj)
        return rt_const_cstr("");
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    if (!ctx->trapped || ctx->error[0] == '\0')
        return rt_const_cstr("");
    return rt_string_from_bytes(ctx->error, strlen(ctx->error));
}

/// @brief Join the underlying thread of a safe-started thread.
void rt_thread_safe_join(void *obj)
{
    if (!obj)
        rt_trap("Thread.SafeJoin: null object");
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    if (ctx->thread)
        rt_thread_join(ctx->thread);
}

/// @brief Get the thread ID of a safe-started thread.
int64_t rt_thread_safe_get_id(void *obj)
{
    if (!obj)
        return 0;
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    if (ctx->thread)
        return rt_thread_get_id(ctx->thread);
    return 0;
}

/// @brief Check if a safe-started thread is alive.
int8_t rt_thread_safe_is_alive(void *obj)
{
    if (!obj)
        return 0;
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    if (ctx->thread)
        return rt_thread_get_is_alive(ctx->thread);
    return 0;
}
