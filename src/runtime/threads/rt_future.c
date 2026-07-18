//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_future.c
// Purpose: Implements the Future/Promise async result pattern for the
//          Zanna.Threads.Future and Zanna.Threads.Promise classes. A Promise
//          is the write end; a Future is the read end. Completion is signalled
//          via a condition variable; waiting is blocking until resolved.
//
// Key invariants:
//   - A Promise can be resolved (value) or rejected (error string) exactly once.
//   - Public duplicate completion traps immediately; internal worker try-set
//     completion reports false and preserves the first result.
//   - Future.Await blocks until the promise is resolved or rejected.
//   - Future.TryGet returns immediately: the value if done, NULL if pending.
//   - The done flag is sticky; once set it is never cleared.
//   - Native C-string rejection still publishes an error state when allocating
//     the optional diagnostic String fails.
//   - Win32 uses CRITICAL_SECTION + CONDITION_VARIABLE; POSIX uses pthreads.
//
// Ownership/Lifetime:
//   - The promise_impl is shared between the Promise and Future objects.
//   - The resolved value (void*) is retained by the promise until consumed.
//   - The error string is retained by the promise and released on finalize.
//   - Both Promise and Future hold a pointer to the shared promise_impl, which
//     is freed when neither object is alive.
//
// Links: src/runtime/threads/rt_future.h (public API),
//        src/runtime/threads/rt_async.h (higher-level async combinators),
//        src/runtime/threads/rt_cancellation.h (cancellation integration)
//
//===----------------------------------------------------------------------===//

#include "rt_future.h"
#include "rt_gc.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_platform.h"
#include "rt_string_internal.h"
#include "rt_threads.h"

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RT_PLATFORM_WINDOWS
#include "rt_win32_wait.h"
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#if RT_PLATFORM_MACOS
extern int pthread_cond_timedwait_relative_np(pthread_cond_t *cond,
                                              pthread_mutex_t *mutex,
                                              const struct timespec *rel_time);
#endif
#endif

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
#if RT_PLATFORM_WINDOWS
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE cond;
#else
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
    void *value;
    rt_string error;
    int8_t done;
    int8_t is_error;
    int8_t owns_value;
    int8_t cond_uses_monotonic;
    int8_t sync_alive;
    void *future; // Cached future object
    struct future_listener *listeners;
    struct future_listener *listeners_tail;
} promise_impl;

typedef struct {
    promise_impl *promise;
} future_impl;

typedef struct future_listener {
    void (*callback)(void *future, void *ctx);
    void (*cancel)(void *ctx);
    void *ctx;
    void *future_obj;
    struct future_listener *next;
} future_listener;

static void promise_lock(promise_impl *p) {
#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif
}

static void promise_unlock(promise_impl *p) {
#if RT_PLATFORM_WINDOWS
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_unlock(&p->mutex);
#endif
}

/// @brief Validate-and-cast a handle to promise_impl.
/// @details Wrong-type handles always trap; a NULL handle traps only when
///          @p trap_on_null is set (callers that tolerate NULL pass 0).
/// @return The promise, or NULL.
static promise_impl *promise_require(void *obj, int8_t trap_on_null) {
    if (!obj) {
        if (trap_on_null)
            rt_trap("Promise: null object");
        return NULL;
    }
    if (!rt_obj_is_instance(obj, RT_PROMISE_CLASS_ID, sizeof(promise_impl))) {
        rt_trap("Promise: invalid object");
        return NULL;
    }
    return (promise_impl *)obj;
}

/// @brief Validate a Promise handle without invoking the trap dispatcher.
/// @details Internal producer-completion paths use this predicate after they
///          have already left an operation recovery frame. Treating NULL,
///          forged, freed, or wrong-class pointers as an ordinary miss keeps
///          their best-effort settlement contract genuinely non-throwing.
///          Callers must still own a live reference when they expect success;
///          the registry-backed instance check makes stale pointers safe to
///          reject without dereferencing them.
/// @param obj Candidate Promise object pointer.
/// @return Valid Promise payload, or NULL when @p obj is not a live Promise.
static promise_impl *promise_try(void *obj) {
    if (!obj || !rt_obj_is_instance(obj, RT_PROMISE_CLASS_ID, sizeof(promise_impl)))
        return NULL;
    return (promise_impl *)obj;
}

/// @brief Validate-and-cast a handle to future_impl (mirror of
///        promise_require). NULL traps only when @p trap_on_null is set.
/// @return The future, or NULL.
static future_impl *future_require(void *obj, int8_t trap_on_null) {
    if (!obj) {
        if (trap_on_null)
            rt_trap("Future: null object");
        return NULL;
    }
    if (!rt_obj_is_instance(obj, RT_FUTURE_CLASS_ID, sizeof(future_impl))) {
        rt_trap("Future: invalid object");
        return NULL;
    }
    return (future_impl *)obj;
}

/// @brief Release a retained Future / Promise / value reference; free when refcount hits zero.
/// @details Common ownership-discipline helper used by paths that take a
///          temporary ref for the duration of an async wait. NULL @p obj
///          is a no-op.
static void future_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void future_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    const char *err = rt_trap_get_error();
    snprintf(buffer, buffer_size, "%s", err && err[0] ? err : fallback);
}

/// @brief Release a waiter's temporary Future reference and report a native wait failure.
/// @details Condition-variable failures are not timeouts and must not be
///          silently retried: retrying a permanent `EINVAL`/`EPERM` error can
///          spin forever. The helper runs only after any held Promise mutex has
///          been released. Returning embedder trap hooks receive control back
///          with the waiter's ownership already balanced.
/// @param obj Retained Future object held for the wait operation.
/// @param operation Stable API name included in the diagnostic.
/// @param native_error POSIX errno-style result or Win32 error code.
static void future_report_wait_error(void *obj, const char *operation, unsigned long native_error) {
    char message[160];
    snprintf(message,
             sizeof(message),
             "%s: native condition wait failed (%lu)",
             operation ? operation : "Future.Wait",
             native_error);
    future_release_object(obj);
    rt_trap(message);
}

static void future_retain_object_or_cleanup(void *value, void *cleanup_obj, const char *fallback) {
    if (!value)
        return;

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        future_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        future_release_object(cleanup_obj);
        rt_trap(saved_error);
        return;
    }

    rt_obj_retain_maybe(value);
    rt_trap_clear_recovery();
}

static rt_string future_ref_string_or_cleanup(rt_string value,
                                              void *cleanup_obj,
                                              const char *fallback) {
    if (!value)
        return NULL;

    rt_string retained = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        future_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        future_release_object(cleanup_obj);
        rt_trap(saved_error);
        return NULL;
    }

    retained = rt_string_ref(value);
    rt_trap_clear_recovery();
    return retained;
}

/// @brief Promote the Promise's weak cached-Future pointer while holding its mutex.
/// @details The cached pointer does not own the Future. A last release can set
///          the Future's count to zero before its finalizer acquires the Promise
///          mutex to clear the cache. The heap's atomic live-retain operation
///          distinguishes that state from a promotable object without trapping
///          or reviving a zero-reference payload.
/// @param future_obj Cached Future payload to promote.
/// @return 1 for a retained mortal Future, 2 for an immortal payload, 0 for a
///         dead/stale payload, or -1 when the live refcount cannot be increased.
static int32_t future_promote_cached_locked(void *future_obj) {
    return future_obj ? rt_heap_try_retain_live(future_obj) : 0;
}

#if !RT_PLATFORM_WINDOWS
typedef struct {
    struct timespec deadline;
} future_deadline_t;

/// @brief Initialize a pthread condition variable, preferring `CLOCK_MONOTONIC` when available so
/// timed waits aren't disturbed by wall-clock jumps (DST, NTP). Sets `*uses_monotonic` to record
/// which clock the caller must consult for deadline math. macOS lacks per-cond clock selection
/// (uses `pthread_cond_timedwait_relative_np` instead), so it always sets `uses_monotonic=1` and
/// relies on the relative-deadline timer path.
static int future_cond_init(pthread_cond_t *cond, int8_t *uses_monotonic) {
    if (uses_monotonic)
        *uses_monotonic = 0;
#if RT_PLATFORM_MACOS
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

/// @brief Read "now" from whichever clock the cond uses (monotonic or realtime). Falls back to
/// REALTIME on macOS when the monotonic path is unavailable.
static struct timespec future_now_clock(int8_t use_monotonic) {
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
#ifdef CLOCK_MONOTONIC
    if (use_monotonic && clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return ts;
#endif
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    return ts;
}

/// @brief Compute the absolute deadline for `pthread_cond_timedwait`. Adds `ms` milliseconds to
/// the appropriate clock reading, normalizing nanosecond carry into the seconds field.
static future_deadline_t future_deadline_abs_from_now(int64_t ms, int8_t use_monotonic) {
    future_deadline_t d;
    d.deadline = future_now_clock(use_monotonic);
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

#if RT_PLATFORM_MACOS
/// @brief macOS-only: compute remaining ms until `deadline` for the relative-wait API. Returns 0
/// once the deadline has passed (signalling immediate timeout to the caller).
static int64_t future_remaining_ms(future_deadline_t deadline, int8_t use_monotonic) {
    struct timespec now = future_now_clock(use_monotonic);
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

/// @brief Cross-platform pthread `cond_timedwait` wrapper: macOS uses the relative-time API
/// (because per-cond CLOCK_MONOTONIC isn't available there), other platforms use the standard
/// absolute-deadline API. Returns ETIMEDOUT on expiry, 0 on signal.
static int future_cond_timedwait_deadline(pthread_cond_t *cond,
                                          pthread_mutex_t *mutex,
                                          future_deadline_t deadline,
                                          int8_t use_monotonic) {
#if RT_PLATFORM_MACOS
    int64_t remaining = future_remaining_ms(deadline, use_monotonic);
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
#endif

/// @brief Run every listener's callback, release its retained future-object reference, and free
/// the listener node. Walks the linked list iteratively. **Lock-free:** caller must have already
/// detached the list from `promise_impl.listeners` and released the promise mutex (notification
/// happens outside the critical section to avoid blocking other threads on long callbacks).
static void future_invoke_listener(future_listener *listener) {
    if (!listener || !listener->callback)
        return;

    int trapped = 0;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        listener->callback(listener->future_obj, listener->ctx);
    } else {
        trapped = 1;
    }
    rt_trap_clear_recovery();

    if (trapped && listener->cancel) {
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            listener->cancel(listener->ctx);
        }
        rt_trap_clear_recovery();
    }
}

/// @brief Invoke every queued continuation in a future's listener chain (in
///        order) when the future settles, freeing each node as it runs.
static void future_notify_listeners(future_listener *listeners) {
    while (listeners) {
        future_listener *next = listeners->next;
        future_invoke_listener(listeners);

        if (rt_obj_release_check0(listeners->future_obj))
            rt_obj_free(listeners->future_obj);
        free(listeners);
        listeners = next;
    }
}

/// @brief Cancel and release listeners from an abandoned Promise.
/// @details Abandonment is not successful completion, so callbacks are not
///          invoked. Optional cancellation hooks run behind independent trap
///          boundaries, then each listener's strong Future reference is
///          released. This also makes pending continuation cycles collectible.
/// @param listeners Detached listener chain, or NULL.
static void future_cancel_listeners(future_listener *listeners) {
    while (listeners) {
        future_listener *next = listeners->next;
        if (listeners->cancel) {
            jmp_buf recovery;
            rt_trap_set_recovery(&recovery);
            if (setjmp(recovery) == 0)
                listeners->cancel(listeners->ctx);
            rt_trap_clear_recovery();
        }
        future_release_object(listeners->future_obj);
        free(listeners);
        listeners = next;
    }
}

/// @brief Detach the FIFO listener chain from a locked Promise in O(1).
/// @details Clearing both endpoints preserves the empty-list invariant and
///          allows subsequent completion code to invoke callbacks after the
///          Promise mutex is released.
/// @param p Locked Promise implementation.
/// @return Previous listener head, or NULL when no listener was registered.
static future_listener *promise_detach_listeners_locked(promise_impl *p) {
    future_listener *listeners = p ? p->listeners : NULL;
    if (p) {
        p->listeners = NULL;
        p->listeners_tail = NULL;
    }
    return listeners;
}

//=============================================================================
// Promise Implementation
//=============================================================================

static void future_finalizer(void *obj);

/// @brief Enumerate strong managed edges owned by a Promise.
/// @details The Promise mutex serializes traversal with value settlement and
///          listener mutation. The callback visits the owned result and every
///          listener-retained Future; the cached Future pointer is deliberately
///          weak and is not visited. A finalized Promise has destroyed its
///          native synchronization state and therefore reports no edges.
static void promise_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    promise_impl *p = (promise_impl *)obj;
    if (!p || !visitor || !__atomic_load_n(&p->sync_alive, __ATOMIC_ACQUIRE))
        return;
    promise_lock(p);
    if (p->owns_value && p->value)
        visitor(p->value, ctx);
    for (future_listener *listener = p->listeners; listener; listener = listener->next)
        if (listener->future_obj)
            visitor(listener->future_obj, ctx);
    promise_unlock(p);
}

/// @brief Enumerate the Promise strongly owned by a Future wrapper.
/// @details The edge is immutable until finalization. It is cleared before the
///          Future drops its Promise reference, so post-finalizer reclaim
///          traversal observes no outgoing edge.
static void future_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    future_impl *f = (future_impl *)obj;
    if (f && visitor && f->promise)
        visitor(f->promise, ctx);
}

/// @brief GC finalizer: release the resolved value (if owned), error string (if any), notify any
/// remaining listeners (so callers waiting on `_on_complete` get a "promise abandoned" callback),
/// then destroy the platform mutex + condvar. Symmetric across Win32 (DeleteCriticalSection)
/// and POSIX (pthread_mutex_destroy + pthread_cond_destroy).
static void promise_finalizer(void *obj) {
    promise_impl *p = (promise_impl *)obj;
    if (!p)
        return;

    void *value = NULL;
    rt_string error = NULL;
    int8_t owns_value = 0;
    future_listener *listeners = NULL;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif
    value = p->value;
    error = p->error;
    owns_value = p->owns_value;
    listeners = promise_detach_listeners_locked(p);
    p->value = NULL;
    p->error = NULL;
    p->owns_value = 0;
    p->future = NULL;
    p->done = 1;
    p->is_error = 1;
    __atomic_store_n(&p->sync_alive, 0, __ATOMIC_RELEASE);
#if RT_PLATFORM_WINDOWS
    WakeAllConditionVariable(&p->cond);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->mutex);
#endif

    if (owns_value && value && rt_obj_release_check0(value))
        rt_obj_free(value);
    if (error)
        rt_str_release_maybe(error);
    future_cancel_listeners(listeners);
#if RT_PLATFORM_WINDOWS
    DeleteCriticalSection(&p->mutex);
#else
    pthread_mutex_destroy(&p->mutex);
    pthread_cond_destroy(&p->cond);
#endif
}

/// @brief Create a new Promise that can be resolved with a value or error from any thread.
void *rt_promise_new(void) {
    promise_impl *p =
        (promise_impl *)rt_obj_new_i64(RT_PROMISE_CLASS_ID, (int64_t)sizeof(promise_impl));
    if (!p) {
        rt_trap("Promise: memory allocation failed");
        return NULL;
    }

#if RT_PLATFORM_WINDOWS
    InitializeCriticalSection(&p->mutex);
    InitializeConditionVariable(&p->cond);
#else
    if (pthread_mutex_init(&p->mutex, NULL) != 0) {
        if (rt_obj_release_check0(p))
            rt_obj_free(p);
        rt_trap("Promise: mutex initialization failed");
        return NULL;
    }
    if (future_cond_init(&p->cond, &p->cond_uses_monotonic) != 0) {
        (void)pthread_mutex_destroy(&p->mutex);
        if (rt_obj_release_check0(p))
            rt_obj_free(p);
        rt_trap("Promise: condition initialization failed");
        return NULL;
    }
#endif

    p->value = NULL;
    p->error = NULL;
    p->done = 0;
    p->is_error = 0;
    p->owns_value = 0;
    p->cond_uses_monotonic = 0;
    p->sync_alive = 1;
    p->future = NULL;
    p->listeners = NULL;
    p->listeners_tail = NULL;

    rt_obj_set_finalizer(p, promise_finalizer);
    {
        char saved_error[256];
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) != 0) {
            future_save_trap_error(saved_error, sizeof(saved_error), "Promise: GC tracking failed");
            rt_trap_clear_recovery();
            future_release_object(p);
            rt_trap(saved_error);
            return NULL;
        }
        rt_gc_track(p, promise_traverse);
        if (!rt_gc_is_tracked(p))
            rt_trap("Promise: GC tracking failed");
        rt_trap_clear_recovery();
    }
    return p;
}

/// @brief Get the Future associated with this Promise (the read-side of the async result).
void *rt_promise_get_future(void *obj) {
    promise_impl *p = promise_require(obj, 1);
    if (!p)
        return NULL;
    rt_obj_retain_maybe(obj);

    for (;;) {
        promise_lock(p);
        void *cached = p->future;
        if (cached) {
            const int32_t promoted = future_promote_cached_locked(cached);
            if (promoted == 0 && p->future == cached)
                p->future = NULL;
            promise_unlock(p);
            if (promoted < 0) {
                future_release_object(obj);
                rt_trap("refcount overflow");
                return NULL;
            }
            if (promoted == 0)
                continue;
            future_release_object(obj);
            return cached;
        }
        promise_unlock(p);

        future_impl *candidate = NULL;
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) != 0) {
            char saved_error[256];
            future_save_trap_error(saved_error, sizeof(saved_error), "Future: allocation failed");
            rt_trap_clear_recovery();
            if (candidate && rt_obj_release_check0(candidate))
                rt_obj_free(candidate);
            future_release_object(obj);
            rt_trap(saved_error);
            return NULL;
        }

        candidate = (future_impl *)rt_obj_new_i64(RT_FUTURE_CLASS_ID, (int64_t)sizeof(future_impl));
        if (!candidate) {
            rt_trap("Future: memory allocation failed");
            rt_trap_clear_recovery();
            future_release_object(obj);
            return NULL;
        }
        candidate->promise = p;
        rt_obj_retain_maybe(p); // Future holds a reference to prevent premature GC of promise
        rt_obj_set_finalizer(candidate, future_finalizer);
        rt_gc_track(candidate, future_traverse);
        if (!rt_gc_is_tracked(candidate))
            rt_trap("Future: GC tracking failed");
        rt_trap_clear_recovery();

        promise_lock(p);
        if (!p->future) {
            p->future = candidate;
            promise_unlock(p);
            future_release_object(obj);
            return candidate;
        }
        promise_unlock(p);

        if (rt_obj_release_check0(candidate))
            rt_obj_free(candidate);
    }
}

/// @brief Future-side GC finalizer: clear the back-pointer in the promise (so `get_future()`
/// returns a fresh handle next time) and release one promise reference. The promise itself
/// is freed by `promise_finalizer` once the last holder (Promise + any cached Futures) drops.
static void future_finalizer(void *obj) {
    future_impl *f = (future_impl *)obj;
    if (!f || !f->promise)
        return;

    promise_impl *p = f->promise;
    f->promise = NULL;
    if (__atomic_load_n(&p->sync_alive, __ATOMIC_ACQUIRE)) {
        promise_lock(p);
        if (p->future == obj)
            p->future = NULL;
        promise_unlock(p);
    }

    future_release_object(p);
}

/// @brief Set a value in the promise, retaining runtime-managed values until consumed/finalized.
void rt_promise_set(void *obj, void *value) {
    promise_impl *p = promise_require(obj, 1);
    if (!p)
        return;
    rt_obj_retain_maybe(obj);
    if (value)
        future_retain_object_or_cleanup(value, obj, "Promise.Set: value retain failed");

    promise_lock(p);

    if (p->done) {
        promise_unlock(p);
        if (value && rt_obj_release_check0(value))
            rt_obj_free(value);
        future_release_object(obj);
        rt_trap("Promise: already completed");
        return;
    }

    p->value = value;
    p->done = 1;
    p->is_error = 0;
    p->owns_value = value ? 1 : 0;
    future_listener *listeners = promise_detach_listeners_locked(p);

#if RT_PLATFORM_WINDOWS
    WakeAllConditionVariable(&p->cond);
#else
    pthread_cond_broadcast(&p->cond);
#endif
    promise_unlock(p);

    future_notify_listeners(listeners);
    future_release_object(obj);
}

/// @brief Resolve the promise with a value the runtime should retain. Kept as an explicit alias for
/// callers that want to document ownership; `rt_promise_set` now has the same retain semantics.
void rt_promise_set_owned(void *obj, void *value) {
    promise_impl *p = promise_require(obj, 1);
    if (!p)
        return;
    rt_obj_retain_maybe(obj);
    if (value)
        future_retain_object_or_cleanup(value, obj, "Promise.SetOwned: value retain failed");

    promise_lock(p);

    if (p->done) {
        promise_unlock(p);
        if (value && rt_obj_release_check0(value))
            rt_obj_free(value);
        future_release_object(obj);
        rt_trap("Promise: already completed");
        return;
    }

    p->value = value;
    p->done = 1;
    p->is_error = 0;
    p->owns_value = value ? 1 : 0;
    future_listener *listeners = promise_detach_listeners_locked(p);

#if RT_PLATFORM_WINDOWS
    WakeAllConditionVariable(&p->cond);
#else
    pthread_cond_broadcast(&p->cond);
#endif
    promise_unlock(p);

    future_notify_listeners(listeners);
    future_release_object(obj);
}

/// @brief Resolve the promise by transferring an existing producer reference.
/// @details This marks the value as promise-owned without retaining it first.
///          It is intended for async callback results where the callback's
///          returned reference is handed directly to the Future.
void rt_promise_set_transferred(void *obj, void *value) {
    promise_impl *p = promise_require(obj, 1);
    if (!p)
        return;
    rt_obj_retain_maybe(obj);

    promise_lock(p);

    if (p->done) {
        promise_unlock(p);
        future_release_object(value);
        future_release_object(obj);
        rt_trap("Promise: already completed");
        return;
    }

    p->value = value;
    p->done = 1;
    p->is_error = 0;
    p->owns_value = value ? 1 : 0;
    future_listener *listeners = promise_detach_listeners_locked(p);

#if RT_PLATFORM_WINDOWS
    WakeAllConditionVariable(&p->cond);
#else
    pthread_cond_broadcast(&p->cond);
#endif
    promise_unlock(p);

    future_notify_listeners(listeners);
    future_release_object(obj);
}

/// @brief Transfer a worker-produced value without propagating duplicate completion.
/// @details Native worker adapters can race cancellation/combinator settlement and
///          must not turn an already-settled result into a second trap from inside
///          their recovery handler. This variant consumes the producer's value
///          reference in both outcomes, publishes successful completion under the
///          Promise mutex, and invokes detached listeners after unlocking.
/// @param obj Producer-owned Promise kept alive by the caller.
/// @param value Runtime-managed reference being handed off, or NULL.
/// @return One when published; zero when invalid or already completed. The
///         supplied value reference is consumed in every outcome.
int8_t rt_promise_try_set_transferred(void *obj, void *value) {
    promise_impl *p = promise_try(obj);
    if (!p) {
        future_release_object(value);
        return 0;
    }

    promise_lock(p);
    if (p->done) {
        promise_unlock(p);
        future_release_object(value);
        return 0;
    }

    p->value = value;
    p->error = NULL;
    p->done = 1;
    p->is_error = 0;
    p->owns_value = value ? 1 : 0;
    future_listener *listeners = promise_detach_listeners_locked(p);

#if RT_PLATFORM_WINDOWS
    WakeAllConditionVariable(&p->cond);
#else
    pthread_cond_broadcast(&p->cond);
#endif
    promise_unlock(p);

    future_notify_listeners(listeners);
    return 1;
}

/// @brief Complete the promise with an error; wakes all waiting futures.
void rt_promise_set_error(void *obj, rt_string error) {
    promise_impl *p = promise_require(obj, 1);
    if (!p)
        return;
    rt_string stored_error = NULL;

    jmp_buf setup_recovery;
    rt_trap_set_recovery(&setup_recovery);
    if (setjmp(setup_recovery) != 0) {
        char saved_error[256];
        future_save_trap_error(
            saved_error, sizeof(saved_error), "Promise.SetError: error retain failed");
        rt_trap_clear_recovery();
        if (stored_error)
            rt_str_release_maybe(stored_error);
        rt_trap(saved_error);
        return;
    }

    if (error) {
        const char *err_str = rt_string_cstr(error);
        stored_error = err_str ? rt_string_from_bytes(err_str, rt_string_len_bytes(error))
                               : rt_const_cstr("Unknown error");
    } else {
        stored_error = rt_const_cstr("Unknown error");
    }
    rt_obj_retain_maybe(obj);
    rt_trap_clear_recovery();

    promise_lock(p);

    if (p->done) {
        promise_unlock(p);
        if (stored_error)
            rt_str_release_maybe(stored_error);
        future_release_object(obj);
        rt_trap("Promise: already completed");
        return;
    }

    p->error = stored_error;
    p->done = 1;
    p->is_error = 1;
    p->owns_value = 0;
    future_listener *listeners = promise_detach_listeners_locked(p);

#if RT_PLATFORM_WINDOWS
    WakeAllConditionVariable(&p->cond);
#else
    pthread_cond_broadcast(&p->cond);
#endif
    promise_unlock(p);

    future_notify_listeners(listeners);
    future_release_object(obj);
}

/// @brief Reject a producer-owned Promise without letting diagnostic allocation escape.
/// @details The ordinary public SetError contract copies a managed String and
///          traps on duplicate completion. Native worker boundaries need a
///          stronger progress guarantee: after catching the worker's original
///          trap, a second OOM while copying that diagnostic must not recurse
///          through the same recovery frame or leave waiters pending forever.
///          This function therefore catches only the optional String-copy trap,
///          publishes a message-less error as its allocation-free fallback,
///          and reports duplicate completion with a zero result.
/// @param obj Producer-owned Promise object pointer kept live by the caller.
/// @param error NUL-terminated diagnostic, or NULL for the generic fallback.
/// @return One when the Promise transitioned to error, otherwise zero. Promise
///         validation and duplicate completion do not raise a trap.
int8_t rt_promise_try_set_error_cstr(void *obj, const char *error) {
    promise_impl *p = promise_try(obj);
    if (!p)
        return 0;

    promise_lock(p);
    if (p->done) {
        promise_unlock(p);
        return 0;
    }
    promise_unlock(p);

    rt_string stored_error = NULL;
    if (error && error[0]) {
        jmp_buf copy_recovery;
        rt_trap_set_recovery(&copy_recovery);
        if (setjmp(copy_recovery) == 0) {
            stored_error = rt_string_from_bytes(error, strlen(error));
            rt_trap_clear_recovery();
        } else {
            rt_trap_clear_recovery();
            stored_error = NULL;
        }
    }

    promise_lock(p);
    if (p->done) {
        promise_unlock(p);
        if (stored_error)
            rt_str_release_maybe(stored_error);
        return 0;
    }

    p->error = stored_error;
    p->value = NULL;
    p->done = 1;
    p->is_error = 1;
    p->owns_value = 0;
    future_listener *listeners = promise_detach_listeners_locked(p);

#if RT_PLATFORM_WINDOWS
    WakeAllConditionVariable(&p->cond);
#else
    pthread_cond_broadcast(&p->cond);
#endif
    promise_unlock(p);

    future_notify_listeners(listeners);
    return 1;
}

/// @brief Check whether the promise has been completed (either Ok or Error).
int8_t rt_promise_is_done(void *obj) {
    promise_impl *p = promise_require(obj, 0);
    if (!p)
        return 0;
    rt_obj_retain_maybe(obj);

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    int8_t result = p->done;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t result = p->done;
    pthread_mutex_unlock(&p->mutex);
#endif

    future_release_object(obj);
    return result;
}

//=============================================================================
// Future Implementation
//=============================================================================

/// @brief Block until the Future has a value and return it (traps if the Future resolved to error).
void *rt_future_get(void *obj) {
    future_impl *f = future_require(obj, 1);
    if (!f)
        return NULL;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    void *result = NULL;
    rt_string error = NULL;
    int8_t is_error = 0;
    int8_t owns_value = 0;
    unsigned long wait_error = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    while (!p->done) {
        if (!SleepConditionVariableCS(&p->cond, &p->mutex, INFINITE)) {
            wait_error = (unsigned long)GetLastError();
            break;
        }
    }
    if (!wait_error && p->is_error) {
        is_error = 1;
        error = p->error;
    } else if (!wait_error) {
        result = p->value;
        owns_value = p->owns_value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    int lock_rc = pthread_mutex_lock(&p->mutex);
    if (lock_rc != 0) {
        future_report_wait_error(obj, "Future.Get", (unsigned long)lock_rc);
        return NULL;
    }
    while (!p->done) {
        int rc = pthread_cond_wait(&p->cond, &p->mutex);
        if (rc != 0) {
            wait_error = (unsigned long)rc;
            break;
        }
    }
    if (!wait_error && p->is_error) {
        is_error = 1;
        error = p->error;
    } else if (!wait_error) {
        result = p->value;
        owns_value = p->owns_value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (wait_error) {
        future_report_wait_error(obj, "Future.Get", wait_error);
        return NULL;
    }

    if (is_error && error)
        error = future_ref_string_or_cleanup(error, obj, "Future.Get: error retain failed");
    if (!is_error && result && owns_value)
        future_retain_object_or_cleanup(result, obj, "Future.Get: value retain failed");

    if (is_error) {
        char error_msg[512];
        error_msg[0] = '\0';
        if (error) {
            const char *err = rt_string_cstr(error);
            int64_t len = rt_string_len_bytes(error);
            if (err && len > 0) {
                size_t copy_len = (size_t)(len < (int64_t)(sizeof(error_msg) - 1)
                                               ? len
                                               : (int64_t)(sizeof(error_msg) - 1));
                memcpy(error_msg, err, copy_len);
                error_msg[copy_len] = '\0';
            }
            rt_str_release_maybe(error);
        }
        future_release_object(obj);
        rt_trap(error_msg[0] ? error_msg : "Future: resolved with error");
        return NULL;
    }

    future_release_object(obj);
    return result;
}

/// @brief Wait up to @p ms milliseconds for the future to complete, returning success.
/// @details If the future completes within the timeout and is not an error, stores
///          the result in *out and returns 1. Returns 0 on timeout or error.
int8_t rt_future_get_for(void *obj, int64_t ms, void **out) {
    if (out)
        *out = NULL;
    if (!obj)
        return 0;
    if (ms <= 0)
        return rt_future_try_get(obj, out);
    future_impl *f = future_require(obj, 0);
    if (!f)
        return 0;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    void *result = NULL;
    int8_t owns_value = 0;
    int8_t success = 0;
    unsigned long wait_error = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    ULONGLONG deadline = rt_win32_deadline_from_now_ms(ms);
    while (!p->done) {
        DWORD remaining = rt_win32_wait_slice_until(deadline);
        if (remaining == 0)
            break;
        if (!SleepConditionVariableCS(&p->cond, &p->mutex, remaining) && !p->done) {
            DWORD error = GetLastError();
            if (error != ERROR_TIMEOUT) {
                wait_error = (unsigned long)error;
                break;
            }
        }
    }
    success = p->done && !p->is_error;
    if (success && out) {
        result = p->value;
        owns_value = p->owns_value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    int lock_rc = pthread_mutex_lock(&p->mutex);
    if (lock_rc != 0) {
        future_report_wait_error(obj, "Future.GetFor", (unsigned long)lock_rc);
        return 0;
    }
    future_deadline_t deadline = future_deadline_abs_from_now(ms, p->cond_uses_monotonic);
    while (!p->done) {
        int rc =
            future_cond_timedwait_deadline(&p->cond, &p->mutex, deadline, p->cond_uses_monotonic);
        if (rc == ETIMEDOUT && !p->done)
            break;
        if (rc != 0) {
            wait_error = (unsigned long)rc;
            break;
        }
    }
    success = p->done && !p->is_error;
    if (success && out) {
        result = p->value;
        owns_value = p->owns_value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (wait_error) {
        future_report_wait_error(obj, "Future.GetFor", wait_error);
        return 0;
    }

    if (success && out) {
        if (result && owns_value)
            future_retain_object_or_cleanup(result, obj, "Future.GetFor: value retain failed");
        *out = result;
    }
    future_release_object(obj);
    return success;
}

/// @brief Check whether the future's underlying promise has been completed.
int8_t rt_future_is_done(void *obj) {
    future_impl *f = future_require(obj, 0);
    if (!f)
        return 0;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    int8_t result = p->done;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t result = p->done;
    pthread_mutex_unlock(&p->mutex);
#endif

    future_release_object(obj);
    return result;
}

/// @brief Check whether the future completed with an error.
int8_t rt_future_is_error(void *obj) {
    future_impl *f = future_require(obj, 0);
    if (!f)
        return 0;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    int8_t result = p->done && p->is_error;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t result = p->done && p->is_error;
    pthread_mutex_unlock(&p->mutex);
#endif

    future_release_object(obj);
    return result;
}

/// @brief Return the error message if the future failed, or empty string otherwise.
rt_string rt_future_get_error(void *obj) {
    future_impl *f = future_require(obj, 0);
    if (!f)
        return rt_const_cstr("");
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    rt_string error = NULL;
    int8_t has_error = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    if (p->done && p->is_error && p->error) {
        error = p->error;
        has_error = 1;
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (p->done && p->is_error && p->error) {
        error = p->error;
        has_error = 1;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    rt_string result = NULL;
    if (has_error)
        result = future_ref_string_or_cleanup(error, obj, "Future.GetError: error retain failed");
    future_release_object(obj);
    return result ? result : rt_const_cstr("");
}

/// @brief Get a value from the future.
int8_t rt_future_try_get(void *obj, void **out) {
    if (out)
        *out = NULL;
    future_impl *f = future_require(obj, 0);
    if (!f)
        return 0;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    void *result = NULL;
    int8_t owns_value = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    int8_t success = p->done && !p->is_error;
    if (success && out) {
        result = p->value;
        owns_value = p->owns_value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t success = p->done && !p->is_error;
    if (success && out) {
        result = p->value;
        owns_value = p->owns_value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (success && out) {
        if (result && owns_value)
            future_retain_object_or_cleanup(result, obj, "Future.TryGet: value retain failed");
        *out = result;
    }
    future_release_object(obj);
    return success;
}

/// @brief Non-blocking value-or-NULL fetch. Returns the resolved value if the future has settled
/// successfully; NULL if pending or errored. Convenient form of `try_get` for callers that don't
/// need to distinguish "not yet" from "errored" (use `is_error` separately if you do).
void *rt_future_try_get_val(void *obj) {
    future_impl *f = future_require(obj, 0);
    if (!f)
        return NULL;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    void *result = NULL;
    int8_t owns_value = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    if (p->done && !p->is_error) {
        result = p->value;
        owns_value = p->owns_value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (p->done && !p->is_error) {
        result = p->value;
        owns_value = p->owns_value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (result && owns_value)
        future_retain_object_or_cleanup(result, obj, "Future.TryGetVal: value retain failed");
    future_release_object(obj);
    return result;
}

/// @brief Non-blocking value fetch as an Option.
/// @details Returns `Some(value)` when the future has completed successfully
///          and `None` when it is pending or rejected. This function reads the
///          promise state directly so a successful NULL payload can be returned
///          as `Some(NULL)` and so owned payloads can be balanced after the
///          Option has retained them.
/// @param obj Future object pointer.
/// @return Opaque Zanna.Option object containing the successful value, or None.
void *rt_future_try_get_option(void *obj) {
    future_impl *f = future_require(obj, 0);
    if (!f)
        return rt_option_none();
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    void *result = NULL;
    int8_t owns_value = 0;
    int8_t success = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    success = p->done && !p->is_error;
    if (success) {
        result = p->value;
        owns_value = p->owns_value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    success = p->done && !p->is_error;
    if (success) {
        result = p->value;
        owns_value = p->owns_value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (!success) {
        future_release_object(obj);
        return rt_option_none();
    }

    if (result && owns_value)
        future_retain_object_or_cleanup(result, obj, "Future.TryGetOption: value retain failed");
    future_release_object(obj);

    void *option = rt_option_some(result);
    if (result && owns_value && rt_obj_release_check0(result))
        rt_obj_free(result);
    return option;
}

/// @brief Bounded-wait variant of `try_get_val`. Waits up to `ms` for the future to resolve;
/// returns the value on success, NULL on timeout or error. Same Win32/POSIX deadline math as
/// `_get_for`, but returns the value directly rather than via an out-parameter.
void *rt_future_get_for_val(void *obj, int64_t ms) {
    if (!obj)
        return NULL;
    if (ms <= 0)
        return rt_future_try_get_val(obj);
    future_impl *f = future_require(obj, 0);
    if (!f)
        return NULL;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    void *result = NULL;
    int8_t owns_value = 0;
    unsigned long wait_error = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    ULONGLONG deadline = rt_win32_deadline_from_now_ms(ms);
    while (!p->done) {
        DWORD remaining = rt_win32_wait_slice_until(deadline);
        if (remaining == 0)
            break;
        if (!SleepConditionVariableCS(&p->cond, &p->mutex, remaining) && !p->done) {
            DWORD error = GetLastError();
            if (error != ERROR_TIMEOUT) {
                wait_error = (unsigned long)error;
                break;
            }
        }
    }
    if (p->done && !p->is_error) {
        result = p->value;
        owns_value = p->owns_value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    int lock_rc = pthread_mutex_lock(&p->mutex);
    if (lock_rc != 0) {
        future_report_wait_error(obj, "Future.GetForVal", (unsigned long)lock_rc);
        return NULL;
    }
    future_deadline_t deadline = future_deadline_abs_from_now(ms, p->cond_uses_monotonic);
    while (!p->done) {
        int rc =
            future_cond_timedwait_deadline(&p->cond, &p->mutex, deadline, p->cond_uses_monotonic);
        if (rc == ETIMEDOUT && !p->done)
            break;
        if (rc != 0) {
            wait_error = (unsigned long)rc;
            break;
        }
    }
    if (p->done && !p->is_error) {
        result = p->value;
        owns_value = p->owns_value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (wait_error) {
        future_report_wait_error(obj, "Future.GetForVal", wait_error);
        return NULL;
    }

    if (result && owns_value)
        future_retain_object_or_cleanup(result, obj, "Future.GetForVal: value retain failed");
    future_release_object(obj);
    return result;
}

/// @brief Wait the future.
void rt_future_wait(void *obj) {
    future_impl *f = future_require(obj, 0);
    if (!f)
        return;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    unsigned long wait_error = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    while (!p->done) {
        if (!SleepConditionVariableCS(&p->cond, &p->mutex, INFINITE)) {
            wait_error = (unsigned long)GetLastError();
            break;
        }
    }
    LeaveCriticalSection(&p->mutex);
#else
    int lock_rc = pthread_mutex_lock(&p->mutex);
    if (lock_rc != 0) {
        future_report_wait_error(obj, "Future.Wait", (unsigned long)lock_rc);
        return;
    }
    while (!p->done) {
        int rc = pthread_cond_wait(&p->cond, &p->mutex);
        if (rc != 0) {
            wait_error = (unsigned long)rc;
            break;
        }
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (wait_error) {
        future_report_wait_error(obj, "Future.Wait", wait_error);
        return;
    }

    future_release_object(obj);
}

/// @brief Block until the future completes or the timeout expires.
/// @details Returns 1 if the future is done (regardless of ok/error), 0 on timeout.
int8_t rt_future_wait_for(void *obj, int64_t ms) {
    if (!obj)
        return 0;
    if (ms <= 0)
        return rt_future_is_done(obj);
    future_impl *f = future_require(obj, 0);
    if (!f)
        return 0;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    int8_t result = 0;
    unsigned long wait_error = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    ULONGLONG deadline = rt_win32_deadline_from_now_ms(ms);
    while (!p->done) {
        DWORD remaining = rt_win32_wait_slice_until(deadline);
        if (remaining == 0)
            break;
        if (!SleepConditionVariableCS(&p->cond, &p->mutex, remaining) && !p->done) {
            DWORD error = GetLastError();
            if (error != ERROR_TIMEOUT) {
                wait_error = (unsigned long)error;
                break;
            }
        }
    }
    result = p->done;
    LeaveCriticalSection(&p->mutex);
#else
    int lock_rc = pthread_mutex_lock(&p->mutex);
    if (lock_rc != 0) {
        future_report_wait_error(obj, "Future.WaitFor", (unsigned long)lock_rc);
        return 0;
    }
    future_deadline_t deadline = future_deadline_abs_from_now(ms, p->cond_uses_monotonic);
    while (!p->done) {
        int rc =
            future_cond_timedwait_deadline(&p->cond, &p->mutex, deadline, p->cond_uses_monotonic);
        if (rc == ETIMEDOUT && !p->done)
            break;
        if (rc != 0) {
            wait_error = (unsigned long)rc;
            break;
        }
    }
    result = p->done;
    pthread_mutex_unlock(&p->mutex);
#endif

    if (wait_error) {
        future_report_wait_error(obj, "Future.WaitFor", wait_error);
        return 0;
    }

    future_release_object(obj);
    return result;
}

/// @brief Register a callback to fire when the future resolves (extended form with cancel hook).
/// **Race-free fast path:** if the promise is already done, invokes the callback synchronously
/// (no listener allocation). Otherwise allocates a listener node, retains the future-object
/// reference, and appends to the promise's listener list. The optional `cancel` callback is
/// invoked if the listener is later removed via `_cancel_listener` — useful for resource
/// cleanup when a continuation is abandoned. Returns 1 on success, 0 on alloc failure.
int8_t rt_future_on_complete_ex(void *obj,
                                void (*callback)(void *future, void *ctx),
                                void *ctx,
                                void (*cancel)(void *ctx)) {
    if (!obj || !callback)
        return 0;
    future_impl *f = future_require(obj, 0);
    if (!f)
        return 0;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;

    future_listener *listener = (future_listener *)calloc(1, sizeof(future_listener));
    if (!listener) {
        future_release_object(obj);
        return 0;
    }
    listener->callback = callback;
    listener->cancel = cancel;
    listener->ctx = ctx;
    listener->future_obj = obj;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (p->done) {
#if RT_PLATFORM_WINDOWS
        LeaveCriticalSection(&p->mutex);
#else
        pthread_mutex_unlock(&p->mutex);
#endif
        future_invoke_listener(listener);

        if (rt_obj_release_check0(listener->future_obj))
            rt_obj_free(listener->future_obj);
        free(listener);
        return 1;
    }

    if (p->listeners_tail)
        p->listeners_tail->next = listener;
    else
        p->listeners = listener;
    p->listeners_tail = listener;

#if RT_PLATFORM_WINDOWS
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_unlock(&p->mutex);
#endif

    return 1;
}

/// @brief Convenience: register a completion callback without a cancel hook. Equivalent to
/// `_on_complete_ex(obj, callback, ctx, NULL)`. Most callers prefer this form.
int8_t rt_future_on_complete(void *obj, void (*callback)(void *future, void *ctx), void *ctx) {
    return rt_future_on_complete_ex(obj, callback, ctx, NULL);
}

/// @brief Remove a previously-registered listener matching `(callback, ctx)`. Linear scan; the
/// first match wins. If found, fires the listener's `cancel` hook (if any) for cleanup, then
/// releases the retained future-object reference. Returns 1 if a listener was removed, 0 otherwise.
int8_t rt_future_cancel_listener(void *obj, void (*callback)(void *future, void *ctx), void *ctx) {
    if (!obj || !callback)
        return 0;
    future_impl *f = future_require(obj, 0);
    if (!f)
        return 0;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    future_listener *removed = NULL;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    future_listener *previous = NULL;
    future_listener **link = &p->listeners;
    while (*link) {
        future_listener *cur = *link;
        if (cur->callback == callback && cur->ctx == ctx) {
            *link = cur->next;
            if (p->listeners_tail == cur)
                p->listeners_tail = previous;
            removed = cur;
            break;
        }
        previous = cur;
        link = &cur->next;
    }

#if RT_PLATFORM_WINDOWS
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_unlock(&p->mutex);
#endif

    if (!removed) {
        future_release_object(obj);
        return 0;
    }

    if (removed->cancel) {
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) == 0) {
            removed->cancel(removed->ctx);
        }
        rt_trap_clear_recovery();
    }
    if (rt_obj_release_check0(removed->future_obj))
        rt_obj_free(removed->future_obj);
    free(removed);
    future_release_object(obj);
    return 1;
}

/// @brief Return the resolved value with a fresh retain when the promise owns it.
/// Returns NULL if pending or errored. Callers that receive a runtime object must
/// release it when done or transfer it to another owner.
void *rt_future_peek_value(void *obj) {
    future_impl *f = future_require(obj, 0);
    if (!f)
        return NULL;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    void *result = NULL;
    int8_t owns_value = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    if (p->done && !p->is_error) {
        result = p->value;
        owns_value = p->owns_value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (p->done && !p->is_error) {
        result = p->value;
        owns_value = p->owns_value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (result && owns_value)
        future_retain_object_or_cleanup(result, obj, "Future.PeekValue: value retain failed");
    future_release_object(obj);
    return result;
}

/// @brief Returns 1 if the resolved value was set via `rt_promise_set_owned` (so the future
/// holds a refcounted reference). Used by combinators like `rt_async` to decide whether to
/// re-retain when forwarding a value to a chained future.
int8_t rt_future_value_is_owned(void *obj) {
    future_impl *f = future_require(obj, 0);
    if (!f)
        return 0;
    rt_obj_retain_maybe(obj);

    promise_impl *p = f->promise;
    int8_t owned = 0;

#if RT_PLATFORM_WINDOWS
    EnterCriticalSection(&p->mutex);
    if (p->done && !p->is_error)
        owned = p->owns_value;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (p->done && !p->is_error)
        owned = p->owns_value;
    pthread_mutex_unlock(&p->mutex);
#endif

    future_release_object(obj);
    return owned;
}
