//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_threads.c
/// @brief OS thread helpers backing Viper.Threads.Thread class.
///
/// This file implements the threading primitives for Viper programs, providing
/// an abstraction over platform-specific threading APIs. On Unix/macOS it uses
/// POSIX threads (pthreads), while Windows support is planned but currently
/// traps with "unsupported".
///
/// **Thread Lifecycle:**
/// ```
///                    ┌─────────────┐
///                    │  Thread.    │
///                    │  Start()    │
///                    └──────┬──────┘
///                           │
///                           ▼
///            ┌──────────────────────────────┐
///            │         RUNNING              │
///            │  (IsAlive = true)            │
///            │                              │
///            │  Entry function executing    │
///            └──────────────┬───────────────┘
///                           │
///                           │ Entry function returns
///                           ▼
///            ┌──────────────────────────────┐
///            │         FINISHED             │
///            │  (IsAlive = false)           │
///            │                              │
///            │  Thread resources released   │
///            │  Join() returns immediately  │
///            └──────────────────────────────┘
/// ```
///
/// **Synchronization Operations:**
///
/// | Method       | Blocks? | Returns                          |
/// |--------------|---------|----------------------------------|
/// | Join()       | Yes     | When thread finishes             |
/// | TryJoin()    | No      | True if finished, false if not   |
/// | JoinFor(ms)  | Up to ms| True if joined, false on timeout |
///
/// **Usage Examples:**
/// ```
/// ' Start a worker thread
/// Dim worker = Thread.Start(Sub() ProcessData())
///
/// ' Wait for completion
/// worker.Join()
/// Print "Worker finished"
///
/// ' Non-blocking check
/// If worker.TryJoin() Then
///     Print "Done!"
/// Else
///     Print "Still working..."
/// End If
///
/// ' Wait with timeout (5 seconds)
/// If worker.JoinFor(5000) Then
///     Print "Completed within 5 seconds"
/// Else
///     Print "Timed out"
/// End If
/// ```
///
/// **Thread ID:**
/// Each thread is assigned a unique, monotonically increasing ID when started.
/// Thread IDs are never reused during the program's lifetime and can be used
/// for logging, debugging, or correlating thread activities.
///
/// **Memory Management:**
/// Threads are garbage-collected objects. The thread object holds a self-reference
/// while running, preventing premature collection. When the entry function returns:
/// 1. The thread marks itself as finished
/// 2. Waiting Join() calls are signaled
/// 3. The self-reference is released
/// 4. The object becomes eligible for GC when no longer referenced
///
/// **Thread Safety:**
/// - Thread.Start() is thread-safe - can be called from any thread
/// - Join operations use mutexes for safe state access
/// - Multiple threads can wait on the same thread (all will be notified)
/// - A thread cannot join itself (traps with error)
/// - Joining a thread that was already joined traps with error
///
/// **Context Inheritance:**
/// New threads inherit the RtContext from their parent thread. This allows:
/// - Access to the same random number generator state
/// - Shared command-line arguments
/// - Consistent runtime environment
///
/// **Platform Support:**
/// | Platform | Status                     |
/// |----------|----------------------------|
/// | macOS    | Full support (pthreads)    |
/// | Linux    | Full support (pthreads)    |
/// | Windows  | Traps (not implemented)    |
///
/// @see rt_monitor.c For synchronization primitives (mutexes, condition vars)
/// @see rt_context.c For runtime context management
///
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include "rt_context.h"
#include "rt_internal.h"
#include "viper/runtime/rt.h"

#include "rt_object.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(_WIN32)

void *rt_thread_start(void *entry, void *arg)
{
    (void)entry;
    (void)arg;
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
    return NULL;
}

void rt_thread_join(void *thread)
{
    (void)thread;
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
}

int8_t rt_thread_try_join(void *thread)
{
    (void)thread;
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
    return 0;
}

int8_t rt_thread_join_for(void *thread, int64_t ms)
{
    (void)thread;
    (void)ms;
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
    return 0;
}

int64_t rt_thread_get_id(void *thread)
{
    (void)thread;
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
    return 0;
}

int8_t rt_thread_get_is_alive(void *thread)
{
    (void)thread;
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
    return 0;
}

void rt_thread_sleep(int64_t ms)
{
    (void)ms;
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
}

void rt_thread_yield(void)
{
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
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
