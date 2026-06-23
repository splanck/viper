//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_threads_posix.c
// Purpose: POSIX (pthread) implementation of the thread runtime: trampoline, join/
//   timed-join (monotonic/Apple relative wait), id/alive queries, sleep/yield.
//   Compiled on non-_WIN32; Windows uses rt_threads_win.c. Shared model in
//   rt_threads_internal.h.
//
// Links: rt_threads_internal.h, rt_threads_win.c, rt_threads_common.c, rt_threads.h
//
//===----------------------------------------------------------------------===//

#include "rt_threads_internal.h"

#if !defined(_WIN32)

#include <pthread.h>
#include <sched.h>
#include <time.h>
#if defined(__APPLE__)
/// @brief macOS-specific relative-time condvar wait (declared here when the SDK header omits it).
extern int pthread_cond_timedwait_relative_np(pthread_cond_t *cond,
                                              pthread_mutex_t *mutex,
                                              const struct timespec *rel_time);
#endif

/// @brief Function pointer type for thread entry functions.
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
/// │ mu                 │ Mutex protecting finished state         │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ cv                 │ Condition variable for Join() waiting  │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ pthread            │ OS thread handle                       │
/// ├────────────────────┼────────────────────────────────────────┤
/// │ finished           │ 1 when entry function has returned     │
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
typedef struct RtThread {
    uint32_t magic;             ///< Runtime tag for validating Thread handles.
    pthread_mutex_t mu;         ///< Mutex protecting state access.
    pthread_cond_t cv;          ///< Condition var for Join() signaling.
    pthread_t pthread;          ///< OS thread handle.
    int finished;               ///< 1 when thread has completed.
    int joined;                 ///< Reserved for ABI/debug compatibility; joins are repeatable.
    int detached;               ///< 1 once pthread_detach has succeeded for the native handle.
    int64_t id;                 ///< Unique thread identifier.
    RtContext *inherited_ctx;   ///< Parent's runtime context.
    rt_thread_entry_fn entry;   ///< User's entry function.
    void *arg;                  ///< Argument to entry function.
    int8_t owns_arg;            ///< 1 when arg is a retained runtime object.
    int8_t cond_uses_monotonic; ///< 1 when cv uses monotonic timed waits.
} RtThread;

/// @brief True if @p obj is a live regular Thread handle (correct class id,
///        size, and RT_THREAD_MAGIC guard) — distinguishes it from SafeThread
///        and stale/foreign pointers before any RtThread deref.
/// @note Defined once per platform backend branch; both copies are identical.
int is_regular_thread_handle(void *obj) {
    return rt_obj_is_instance(obj, RT_THREAD_CLASS_ID, sizeof(RtThread)) &&
           thread_handle_magic(obj) == RT_THREAD_MAGIC;
}

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
static int64_t next_thread_id(void) {
    return (int64_t)__atomic_fetch_add(&g_next_thread_id, 1, __ATOMIC_RELAXED);
}

/// @brief Drop the GC reference held on `t->arg` (only if `t->owns_arg` was set on start).
static void rt_thread_release_owned_arg(RtThread *t) {
    if (!t || !t->owns_arg || !t->arg)
        return;
    if (rt_obj_release_check0(t->arg))
        rt_obj_free(t->arg);
    t->arg = NULL;
    t->owns_arg = 0;
}

/// @brief Initialise a pthread condvar, preferring `CLOCK_MONOTONIC` if available.
///
/// `pthread_cond_timedwait` is normally tied to `CLOCK_REALTIME`,
/// which can jump around (NTP, manual changes). Setting the
/// monotonic clock attribute makes timed waits robust to wall-clock changes.
/// Sets `*uses_monotonic` to 1 if monotonic worked, 0 otherwise.
static int thread_cond_init(pthread_cond_t *cond, int8_t *uses_monotonic) {
    if (uses_monotonic)
        *uses_monotonic = 0;
#if defined(__APPLE__)
    if (uses_monotonic)
        *uses_monotonic = 1;
    return pthread_cond_init(cond, NULL);
#elif defined(CLOCK_MONOTONIC)
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0)
        return pthread_cond_init(cond, NULL);
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0) {
        pthread_condattr_destroy(&attr);
        return pthread_cond_init(cond, NULL);
    }
    {
        if (uses_monotonic)
            *uses_monotonic = 1;
        const int rc = pthread_cond_init(cond, &attr);
        if (rc != 0 && uses_monotonic)
            *uses_monotonic = 0;
        pthread_condattr_destroy(&attr);
        return rc;
    }
#else
    return pthread_cond_init(cond, NULL);
#endif
}

typedef struct {
    struct timespec deadline;
} thread_deadline_t;

/// @brief Read the current `timespec` from the monotonic or realtime clock based on
/// `use_monotonic`.
static struct timespec thread_now_clock(int8_t use_monotonic) {
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
#ifdef CLOCK_MONOTONIC
    if (use_monotonic && clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return ts;
#endif
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    return ts;
}

/// @brief Compute an absolute deadline `ms` milliseconds from now (used by
/// `pthread_cond_timedwait`).
static thread_deadline_t thread_deadline_from_now(int64_t ms, int8_t use_monotonic) {
    thread_deadline_t d;
    d.deadline = thread_now_clock(use_monotonic);
    if (ms <= 0)
        return d;

    int64_t add_sec = ms / 1000;
    long add_nsec = (long)((ms % 1000) * 1000000L);
    int64_t sec_room = (int64_t)LONG_MAX - (int64_t)d.deadline.tv_sec;
    if (add_sec > sec_room || (add_sec == sec_room && d.deadline.tv_nsec > 999999999L - add_nsec)) {
        d.deadline.tv_sec = (time_t)LONG_MAX;
        d.deadline.tv_nsec = 999999999L;
        return d;
    }
    int64_t sec = (int64_t)d.deadline.tv_sec + add_sec;
    int64_t ns = (int64_t)d.deadline.tv_nsec + add_nsec;
    if (ns >= 1000000000) {
        sec += 1;
        ns -= 1000000000;
    }
    d.deadline.tv_sec = (time_t)sec;
    d.deadline.tv_nsec = (long)ns;
    return d;
}

#if defined(__APPLE__)
/// @brief Milliseconds remaining until `deadline` (negative if already past).
static int64_t thread_remaining_ms(thread_deadline_t deadline, int8_t use_monotonic) {
    struct timespec now = thread_now_clock(use_monotonic);
    int64_t sec = (int64_t)deadline.deadline.tv_sec - (int64_t)now.tv_sec;
    int64_t ns = (int64_t)deadline.deadline.tv_nsec - (int64_t)now.tv_nsec;
    if (ns < 0) {
        sec--;
        ns += 1000000000L;
    }
    if (sec < 0)
        return 0;
    if (sec > INT64_MAX / 1000)
        return INT64_MAX;
    return sec * 1000 + ns / 1000000L;
}
#endif

/// @brief Wait on `cond` (with `mutex` held) until `deadline` or signal.
///
/// Picks the right pthread call: `pthread_cond_timedwait` with the
/// monotonic clock when supported, or
/// `pthread_cond_timedwait_relative_np` on macOS as a fallback.
/// @return 0 on signal, ETIMEDOUT on deadline, other errno on failure.
static int thread_cond_timedwait_deadline(pthread_cond_t *cond,
                                          pthread_mutex_t *mutex,
                                          thread_deadline_t deadline,
                                          int8_t use_monotonic) {
#if defined(__APPLE__)
    int64_t remaining = thread_remaining_ms(deadline, use_monotonic);
    if (remaining <= 0)
        return ETIMEDOUT;
    struct timespec rel;
    rel.tv_sec = (time_t)(remaining / 1000);
    rel.tv_nsec = (long)((remaining % 1000) * 1000000L);
    return pthread_cond_timedwait_relative_np(cond, mutex, &rel);
#else
    return pthread_cond_timedwait(cond, mutex, &deadline.deadline);
#endif
}

/// @brief Finalizer for RtThread objects, called during garbage collection.
///
/// Cleans up the mutex and condition variable allocated during thread creation.
/// The pthread handle itself doesn't need cleanup since we detach threads.
///
/// @param obj Pointer to the RtThread object. May be NULL (no-op).
static void rt_thread_finalize(void *obj) {
    if (!obj)
        return;
    RtThread *t = (RtThread *)obj;
    t->magic = 0;
    rt_thread_release_owned_arg(t);
    (void)pthread_mutex_destroy(&t->mu);
    (void)pthread_cond_destroy(&t->cv);
}

/// @brief pthread entry function — installs the inherited runtime context, runs the user entry,
/// then signals join-waiters.
///
/// All threads created by `rt_thread_start*` go through this
/// shim so they automatically:
///   1. Install the parent's `RtContext` (so the GC, traps, and
///      thread-local state behave consistently).
///   2. Mark the `RtThread` as `finished` and broadcast on the
///      join condvar so blocking `Thread.Join` returns.
///   3. Drop the self-reference taken in `rt_thread_start_impl`.
/// Safe threads install trap recovery in `safe_thread_entry`.
static void *rt_thread_trampoline(void *p) {
    RtThread *t = (RtThread *)p;
    if (t && t->inherited_ctx)
        rt_set_current_context(t->inherited_ctx);
    if (t && t->entry)
        t->entry(t->arg);
    if (t)
        rt_thread_release_owned_arg(t);
    rt_set_current_context(NULL);

    if (t) {
        pthread_mutex_lock(&t->mu);
        t->finished = 1;
        pthread_cond_broadcast(&t->cv);
        if (!t->detached) {
            (void)pthread_detach(pthread_self());
            t->detached = 1;
        }
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
static RtThread *require_thread(void *thread, const char *what) {
    if (!thread) {
        rt_trap(what ? what : "Thread: null thread");
        return NULL;
    }
    if (!is_regular_thread_handle(thread)) {
        rt_trap("Thread: invalid thread handle");
        return NULL;
    }
    return (RtThread *)thread;
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
static void *rt_thread_start_impl(void *entry, void *arg, int8_t retain_arg) {
    if (!entry)
        rt_trap("Thread.Start: null entry");
    if (!entry)
        return NULL;

    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();

    RtThread *t = (RtThread *)rt_obj_new_i64(RT_THREAD_CLASS_ID, (int64_t)sizeof(RtThread));
    if (!t)
        rt_trap("Thread.Start: failed to create thread");
    if (!t)
        return NULL;

    if (pthread_mutex_init(&t->mu, NULL) != 0) {
        if (rt_obj_release_check0(t))
            rt_obj_free(t);
        rt_trap("Thread.Start: failed to initialize thread state");
        return NULL;
    }
    if (thread_cond_init(&t->cv, &t->cond_uses_monotonic) != 0) {
        (void)pthread_mutex_destroy(&t->mu);
        if (rt_obj_release_check0(t))
            rt_obj_free(t);
        rt_trap("Thread.Start: failed to initialize thread state");
        return NULL;
    }

    t->finished = 0;
    t->joined = 0;
    t->detached = 0;
    t->magic = RT_THREAD_MAGIC;
    t->id = next_thread_id();
    t->inherited_ctx = ctx;
    t->entry = (rt_thread_entry_fn)entry;
    t->arg = arg;
    t->owns_arg = 0;
    rt_obj_set_finalizer(t, rt_thread_finalize);
    if (retain_arg && arg) {
        thread_retain_owned_arg_or_release(arg, t, "Thread.StartOwned: arg retain failed");
        t->owns_arg = 1;
    }

    // Hold a self-reference until the thread exits.
    thread_retain_self_or_release(t, "Thread.Start: self retain failed");

    if (pthread_create(&t->pthread, NULL, rt_thread_trampoline, t) != 0) {
        // Drop thread self-reference and the caller-visible reference, then trap.
        thread_release_object(t);
        thread_release_object(t);
        rt_trap("Thread.Start: failed to create thread");
        return NULL;
    }

    // Detach so OS resources are reclaimed even if the thread is never joined. If the initial
    // detach fails, the worker retries by detaching itself immediately before it exits.
    {
        const int detach_rc = pthread_detach(t->pthread);
        pthread_mutex_lock(&t->mu);
        if (detach_rc == 0)
            t->detached = 1;
        pthread_mutex_unlock(&t->mu);
    }
    return t;
}

/// @brief POSIX `Thread.Start` — see Win32 version above for semantics.
void *rt_thread_start(void *entry, void *arg) {
    return rt_thread_start_impl(entry, arg, 0);
}

/// @brief POSIX `Thread.StartOwned` — see Win32 version above.
void *rt_thread_start_owned(void *entry, void *arg) {
    return rt_thread_start_impl(entry, arg, 1);
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
/// - Traps if a thread tries to join itself (deadlock prevention)
///
/// @param thread The Thread object to wait for. Must not be NULL.
///
/// @note Blocks until the thread finishes - use JoinFor for timeouts.
/// @note A thread cannot join itself (would deadlock).
/// @note Multiple threads can wait on the same thread - all will be notified.
///
/// @see rt_thread_try_join For non-blocking check
/// @see rt_thread_join_for For waiting with timeout
void rt_thread_join(void *thread) {
    if (is_safe_thread_handle(thread)) {
        rt_thread_safe_join(thread);
        return;
    }
    RtThread *t = require_thread(thread, "Thread.Join: null thread");
    if (!t)
        return;
    rt_obj_retain_maybe(thread);

    pthread_mutex_lock(&t->mu);
    if (!t->finished && pthread_equal(pthread_self(), t->pthread)) {
        pthread_mutex_unlock(&t->mu);
        thread_release_object(thread);
        rt_trap("Thread.Join: cannot join self");
        return;
    }
    while (!t->finished) {
        (void)pthread_cond_wait(&t->cv, &t->mu);
    }
    pthread_mutex_unlock(&t->mu);
    thread_release_object(thread);
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
/// @return 1 (true) if the thread was already finished.
///         0 (false) if the thread is still running.
///
/// @note Traps if thread is NULL.
/// @note Traps if called on self.
///
/// @see rt_thread_join For blocking wait
/// @see rt_thread_join_for For waiting with timeout
int8_t rt_thread_try_join(void *thread) {
    if (is_safe_thread_handle(thread)) {
        rt_obj_retain_maybe(thread);
        SafeThreadCtx *ctx = (SafeThreadCtx *)thread;
        void *inner = safe_thread_copy_inner_thread(ctx);
        thread_release_object(thread);
        return thread_try_join_inner_or_release(inner);
    }
    RtThread *t = require_thread(thread, "Thread.TryJoin: null thread");
    if (!t)
        return 0;
    rt_obj_retain_maybe(thread);

    pthread_mutex_lock(&t->mu);
    if (!t->finished && pthread_equal(pthread_self(), t->pthread)) {
        pthread_mutex_unlock(&t->mu);
        thread_release_object(thread);
        rt_trap("Thread.Join: cannot join self");
        return 0;
    }
    if (!t->finished) {
        pthread_mutex_unlock(&t->mu);
        thread_release_object(thread);
        return 0;
    }
    pthread_mutex_unlock(&t->mu);
    thread_release_object(thread);
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
/// @return 1 (true) if the thread finished before the timeout elapsed.
///         0 (false) if the timeout elapsed before the thread finished.
///
/// @note Traps if thread is NULL.
/// @note Traps if called on self.
/// @note If timeout occurs, the caller can wait again later.
///
/// @see rt_thread_join For indefinite waiting
/// @see rt_thread_try_join For immediate check
int8_t rt_thread_join_for(void *thread, int64_t ms) {
    if (is_safe_thread_handle(thread)) {
        if (ms < 0) {
            rt_thread_safe_join(thread);
            return 1;
        }
        rt_obj_retain_maybe(thread);
        SafeThreadCtx *ctx = (SafeThreadCtx *)thread;
        void *inner = safe_thread_copy_inner_thread(ctx);
        thread_release_object(thread);
        return thread_join_for_inner_or_release(inner, ms);
    }
    RtThread *t = require_thread(thread, "Thread.JoinFor: null thread");
    if (!t)
        return 0;

    if (ms < 0) {
        rt_thread_join(thread);
        return 1;
    }

    rt_obj_retain_maybe(thread);

    pthread_mutex_lock(&t->mu);
    if (!t->finished && pthread_equal(pthread_self(), t->pthread)) {
        pthread_mutex_unlock(&t->mu);
        thread_release_object(thread);
        rt_trap("Thread.Join: cannot join self");
        return 0;
    }
    if (ms == 0) {
        if (!t->finished) {
            pthread_mutex_unlock(&t->mu);
            thread_release_object(thread);
            return 0;
        }
        pthread_mutex_unlock(&t->mu);
        thread_release_object(thread);
        return 1;
    }

    const thread_deadline_t deadline = thread_deadline_from_now(ms, t->cond_uses_monotonic);
    while (!t->finished) {
        const int rc =
            thread_cond_timedwait_deadline(&t->cv, &t->mu, deadline, t->cond_uses_monotonic);
        if (rc == ETIMEDOUT && !t->finished) {
            pthread_mutex_unlock(&t->mu);
            thread_release_object(thread);
            return 0;
        }
        if (rc != 0 && rc != ETIMEDOUT) {
            pthread_mutex_unlock(&t->mu);
            thread_release_object(thread);
            rt_trap("Thread.JoinFor: condition wait failed");
            return 0;
        }
    }

    pthread_mutex_unlock(&t->mu);
    thread_release_object(thread);
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
int64_t rt_thread_get_id(void *thread) {
    if (is_safe_thread_handle(thread))
        return rt_thread_safe_get_id(thread);
    RtThread *t = require_thread(thread, "Thread.get_Id: null thread");
    if (!t)
        return 0;
    rt_obj_retain_maybe(thread);
    pthread_mutex_lock(&t->mu);
    int64_t id = t->id;
    pthread_mutex_unlock(&t->mu);
    thread_release_object(thread);
    return id;
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
int8_t rt_thread_get_is_alive(void *thread) {
    if (is_safe_thread_handle(thread))
        return rt_thread_safe_is_alive(thread);
    RtThread *t = require_thread(thread, "Thread.get_IsAlive: null thread");
    if (!t)
        return 0;
    rt_obj_retain_maybe(thread);
    pthread_mutex_lock(&t->mu);
    const int alive = t->finished ? 0 : 1;
    pthread_mutex_unlock(&t->mu);
    thread_release_object(thread);
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
void rt_thread_sleep(int64_t ms) {
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
void rt_thread_yield(void) {
    (void)sched_yield();
}
#endif // !defined(_WIN32)
