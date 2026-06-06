//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_threads_win.c
// Purpose: Windows (Win32) implementation of the thread runtime: native handles,
//   trampoline, join/timed-join, id/alive queries, sleep/yield. Compiled only
//   on _WIN32; POSIX uses rt_threads_posix.c. Shared model in rt_threads_internal.h.
//
// Links: rt_threads_internal.h, rt_threads_posix.c, rt_threads_common.c, rt_threads.h
//
//===----------------------------------------------------------------------===//

#include "rt_threads_internal.h"

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

/// @brief Internal representation of a Viper thread (Windows).
///
/// Uses Windows CRITICAL_SECTION and CONDITION_VARIABLE for synchronization.
/// The thread handle is stored for potential future use (e.g., priority changes)
/// but is not required for join operations since we use condition variables.
typedef struct RtThread {
    uint32_t magic;           ///< Runtime tag for validating Thread handles.
    CRITICAL_SECTION cs;      ///< Critical section protecting state access.
    CONDITION_VARIABLE cv;    ///< Condition var for Join() signaling.
    HANDLE hThread;           ///< OS thread handle.
    DWORD threadId;           ///< OS thread ID for self-join detection.
    int finished;             ///< 1 when thread has completed.
    int joined;               ///< Reserved for ABI/debug compatibility; joins are repeatable.
    int64_t id;               ///< Unique Viper thread identifier.
    RtContext *inherited_ctx; ///< Parent's runtime context.
    rt_thread_entry_fn entry; ///< User's entry function.
    void *arg;                ///< Argument to entry function.
    int8_t owns_arg;          ///< 1 when arg is a retained runtime object.
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
static volatile LONG64 g_next_thread_id_win = 1;

/// @brief Atomically generates the next unique thread ID.
static int64_t next_thread_id_win(void) {
    return (int64_t)InterlockedIncrement64(&g_next_thread_id_win) - 1;
}

/// @brief Finalizer for RtThread objects, called during garbage collection.
static void rt_thread_release_owned_arg_win(RtThread *t) {
    if (!t || !t->owns_arg || !t->arg)
        return;
    if (rt_obj_release_check0(t->arg))
        rt_obj_free(t->arg);
    t->arg = NULL;
    t->owns_arg = 0;
}

/// @brief Finalizer for RtThread objects, called during garbage collection.
static void rt_thread_finalize_win(void *obj) {
    if (!obj)
        return;
    RtThread *t = (RtThread *)obj;
    t->magic = 0;
    rt_thread_release_owned_arg_win(t);
    if (t->hThread)
        CloseHandle(t->hThread);
    DeleteCriticalSection(&t->cs);
    // CONDITION_VARIABLE doesn't need explicit cleanup on Windows
}

/// @brief Thread trampoline that sets up context and runs the entry function.
static DWORD WINAPI rt_thread_trampoline_win(LPVOID p) {
    RtThread *t = (RtThread *)p;
    if (t && t->inherited_ctx)
        rt_set_current_context(t->inherited_ctx);
    if (t && t->entry)
        t->entry(t->arg);
    if (t)
        rt_thread_release_owned_arg_win(t);
    rt_set_current_context(NULL);

    if (t) {
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
static RtThread *require_thread_win(void *thread, const char *what) {
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

/// @brief Windows backend for `rt_thread_start*`.
///
/// Allocates an `RtThread` object, captures the active runtime
/// context (so the new thread inherits it), starts the OS thread
/// via `CreateThread`, and stows the handle for `join`. If
/// `retain_arg` is non-zero we GC-retain `arg` so the thread can
/// access it past the caller's lifetime.
/// Traps on null entry or `CreateThread` failure.
static void *rt_thread_start_impl_win(void *entry, void *arg, int8_t retain_arg) {
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

    InitializeCriticalSection(&t->cs);
    InitializeConditionVariable(&t->cv);

    t->hThread = NULL;
    t->threadId = 0;
    t->finished = 0;
    t->joined = 0;
    t->magic = RT_THREAD_MAGIC;
    t->id = next_thread_id_win();
    t->inherited_ctx = ctx;
    t->entry = (rt_thread_entry_fn)entry;
    t->arg = arg;
    t->owns_arg = 0;
    rt_obj_set_finalizer(t, rt_thread_finalize_win);
    if (retain_arg && arg) {
        thread_retain_owned_arg_or_release(arg, t, "Thread.StartOwned: arg retain failed");
        t->owns_arg = 1;
    }

    // Hold a self-reference until the thread exits.
    thread_retain_self_or_release(t, "Thread.Start: self retain failed");

    t->hThread = CreateThread(NULL, 0, rt_thread_trampoline_win, t, 0, &t->threadId);
    if (!t->hThread) {
        // Drop thread self-reference and the caller-visible reference, then trap.
        thread_release_object(t);
        thread_release_object(t);
        rt_trap("Thread.Start: failed to create thread");
        return NULL;
    }

    return t;
}

/// @brief Public: `Thread.Start(entry, arg)` — start a thread without retaining `arg`.
/// The caller is responsible for keeping `arg` alive until the thread reads it.
void *rt_thread_start(void *entry, void *arg) {
    return rt_thread_start_impl_win(entry, arg, 0);
}

/// @brief Public: start a thread that owns its argument (GC-retains it for the thread's lifetime).
/// Use this when the caller's reference to `arg` may go out of scope before the thread runs.
void *rt_thread_start_owned(void *entry, void *arg) {
    return rt_thread_start_impl_win(entry, arg, 1);
}

/// @brief Block until `thread` finishes executing. Traps if a thread tries to join itself.
void rt_thread_join(void *thread) {
    if (is_safe_thread_handle(thread)) {
        rt_thread_safe_join(thread);
        return;
    }
    RtThread *t = require_thread_win(thread, "Thread.Join: null thread");
    if (!t)
        return;
    rt_obj_retain_maybe(thread);

    EnterCriticalSection(&t->cs);
    if (!t->finished && GetCurrentThreadId() == t->threadId) {
        LeaveCriticalSection(&t->cs);
        thread_release_object(thread);
        rt_trap("Thread.Join: cannot join self");
        return;
    }
    while (!t->finished) {
        SleepConditionVariableCS(&t->cv, &t->cs, INFINITE);
    }
    LeaveCriticalSection(&t->cs);
    thread_release_object(thread);
}

/// @brief Non-blocking join: returns 1 if the thread already finished, 0 if still running.
int8_t rt_thread_try_join(void *thread) {
    if (is_safe_thread_handle(thread)) {
        rt_obj_retain_maybe(thread);
        SafeThreadCtx *ctx = (SafeThreadCtx *)thread;
        void *inner = safe_thread_copy_inner_thread(ctx);
        thread_release_object(thread);
        return thread_try_join_inner_or_release(inner);
    }
    RtThread *t = require_thread_win(thread, "Thread.TryJoin: null thread");
    if (!t)
        return 0;
    rt_obj_retain_maybe(thread);

    EnterCriticalSection(&t->cs);
    if (!t->finished && GetCurrentThreadId() == t->threadId) {
        LeaveCriticalSection(&t->cs);
        thread_release_object(thread);
        rt_trap("Thread.Join: cannot join self");
        return 0;
    }
    if (!t->finished) {
        LeaveCriticalSection(&t->cs);
        thread_release_object(thread);
        return 0;
    }
    LeaveCriticalSection(&t->cs);
    thread_release_object(thread);
    return 1;
}

/// @brief Bounded join — wait at most `ms` milliseconds. Returns 1 on join, 0 on timeout.
/// `ms < 0` waits forever (delegates to `rt_thread_join`); `ms == 0` is a try-join.
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
    RtThread *t = require_thread_win(thread, "Thread.JoinFor: null thread");
    if (!t)
        return 0;

    if (ms < 0) {
        rt_thread_join(thread);
        return 1;
    }

    rt_obj_retain_maybe(thread);

    EnterCriticalSection(&t->cs);
    if (!t->finished && GetCurrentThreadId() == t->threadId) {
        LeaveCriticalSection(&t->cs);
        thread_release_object(thread);
        rt_trap("Thread.Join: cannot join self");
        return 0;
    }
    if (ms == 0) {
        if (!t->finished) {
            LeaveCriticalSection(&t->cs);
            thread_release_object(thread);
            return 0;
        }
        LeaveCriticalSection(&t->cs);
        thread_release_object(thread);
        return 1;
    }

    // Wait with timeout - use a loop to handle spurious wakes
    DWORD timeout_ms = (ms > MAXDWORD) ? MAXDWORD : (DWORD)ms;
    ULONGLONG start = GetTickCount64();
    ULONGLONG elapsed = 0;

    while (!t->finished) {
        DWORD remaining = (elapsed >= timeout_ms) ? 0 : (DWORD)(timeout_ms - elapsed);
        if (remaining == 0) {
            LeaveCriticalSection(&t->cs);
            thread_release_object(thread);
            return 0;
        }

        BOOL ok = SleepConditionVariableCS(&t->cv, &t->cs, remaining);
        if (!ok && GetLastError() == ERROR_TIMEOUT) {
            if (!t->finished) {
                LeaveCriticalSection(&t->cs);
                thread_release_object(thread);
                return 0;
            }
        }
        elapsed = GetTickCount64() - start;
    }

    LeaveCriticalSection(&t->cs);
    thread_release_object(thread);
    return 1;
}

/// @brief The thread's monotonically-increasing per-process ID (from `next_thread_id_*`).
int64_t rt_thread_get_id(void *thread) {
    if (is_safe_thread_handle(thread))
        return rt_thread_safe_get_id(thread);
    RtThread *t = require_thread_win(thread, "Thread.get_Id: null thread");
    if (!t)
        return 0;
    rt_obj_retain_maybe(thread);
    EnterCriticalSection(&t->cs);
    int64_t id = t->id;
    LeaveCriticalSection(&t->cs);
    thread_release_object(thread);
    return id;
}

/// @brief True if the thread is still running; false once its entry function has returned.
int8_t rt_thread_get_is_alive(void *thread) {
    if (is_safe_thread_handle(thread))
        return rt_thread_safe_is_alive(thread);
    RtThread *t = require_thread_win(thread, "Thread.get_IsAlive: null thread");
    if (!t)
        return 0;
    rt_obj_retain_maybe(thread);
    EnterCriticalSection(&t->cs);
    const int alive = t->finished ? 0 : 1;
    LeaveCriticalSection(&t->cs);
    thread_release_object(thread);
    return (int8_t)alive;
}

/// @brief Sleep the calling thread for `ms` milliseconds (clamped to [0, INT32_MAX]).
void rt_thread_sleep(int64_t ms) {
    if (ms < 0)
        ms = 0;
    if (ms > INT32_MAX)
        ms = INT32_MAX;
    rt_sleep_ms((int32_t)ms);
}

/// @brief Hint to the OS that we're willing to yield the CPU (Win32 `SwitchToThread`).
void rt_thread_yield(void) {
    SwitchToThread();
}
#endif // defined(_WIN32)
