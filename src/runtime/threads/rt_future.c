//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_future.c
// Purpose: Implements the Future/Promise async result pattern for the
//          Viper.Threads.Future and Viper.Threads.Promise classes. A Promise
//          is the write end; a Future is the read end. Completion is signalled
//          via a condition variable; waiting is blocking until resolved.
//
// Key invariants:
//   - A Promise can be resolved (value) or rejected (error string) exactly once.
//   - Resolving or rejecting twice traps immediately.
//   - Future.Await blocks until the promise is resolved or rejected.
//   - Future.TryGet returns immediately: the value if done, NULL if pending.
//   - The done flag is sticky; once set it is never cleared.
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
#include "rt_internal.h"
#include "rt_object.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#if defined(__APPLE__)
extern int pthread_cond_timedwait_relative_np(pthread_cond_t *cond,
                                              pthread_mutex_t *mutex,
                                              const struct timespec *rel_time);
#endif
#endif

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
#ifdef _WIN32
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
    void *future; // Cached future object
    struct future_listener *listeners;
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

#ifdef _WIN32
static DWORD future_deadline_ms_from_now(int64_t ms) {
    if (ms <= 0)
        return 0;
    if (ms > (int64_t)MAXDWORD)
        return MAXDWORD;
    return (DWORD)ms;
}
#else
typedef struct {
    struct timespec deadline;
} future_deadline_t;

static int future_cond_init(pthread_cond_t *cond, int8_t *uses_monotonic) {
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

static future_deadline_t future_deadline_abs_from_now(int64_t ms, int8_t use_monotonic) {
    future_deadline_t d;
    d.deadline = future_now_clock(use_monotonic);
    if (ms <= 0)
        return d;

    const int64_t kNsPerMs = 1000000;
    int64_t add_ns = ms * kNsPerMs;
    int64_t sec = (int64_t)d.deadline.tv_sec + add_ns / 1000000000;
    int64_t ns = (int64_t)d.deadline.tv_nsec + add_ns % 1000000000;
    if (ns >= 1000000000) {
        sec += 1;
        ns -= 1000000000;
    }
    d.deadline.tv_sec = (time_t)sec;
    d.deadline.tv_nsec = (long)ns;
    return d;
}

#if defined(__APPLE__)
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
    return sec * 1000 + ns / 1000000L;
}
#endif

static int future_cond_timedwait_deadline(pthread_cond_t *cond,
                                          pthread_mutex_t *mutex,
                                          future_deadline_t deadline,
                                          int8_t use_monotonic) {
#if defined(__APPLE__)
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

static void future_notify_listeners(future_listener *listeners) {
    while (listeners) {
        future_listener *next = listeners->next;
        listeners->callback(listeners->future_obj, listeners->ctx);
        if (rt_obj_release_check0(listeners->future_obj))
            rt_obj_free(listeners->future_obj);
        free(listeners);
        listeners = next;
    }
}

static void *future_export_value_locked(promise_impl *p) {
    if (!p)
        return NULL;
    void *value = p->value;
    if (value && p->owns_value)
        rt_obj_retain_maybe(value);
    return value;
}

//=============================================================================
// Promise Implementation
//=============================================================================

static void future_finalizer(void *obj);

static void promise_finalizer(void *obj) {
    promise_impl *p = (promise_impl *)obj;
    if (!p)
        return;
    if (p->owns_value && p->value && rt_obj_release_check0(p->value))
        rt_obj_free(p->value);
    if (p->error)
        rt_str_release_maybe(p->error);
    future_notify_listeners(p->listeners);
#ifdef _WIN32
    DeleteCriticalSection(&p->mutex);
#else
    pthread_mutex_destroy(&p->mutex);
    pthread_cond_destroy(&p->cond);
#endif
}

/// @brief Create a new Promise that can be resolved with a value or error from any thread.
void *rt_promise_new(void) {
    promise_impl *p = (promise_impl *)rt_obj_new_i64(0, (int64_t)sizeof(promise_impl));
    if (!p)
        rt_trap("Promise: memory allocation failed");

#ifdef _WIN32
    InitializeCriticalSection(&p->mutex);
    InitializeConditionVariable(&p->cond);
#else
    pthread_mutex_init(&p->mutex, NULL);
    future_cond_init(&p->cond, &p->cond_uses_monotonic);
#endif

    p->value = NULL;
    p->error = NULL;
    p->done = 0;
    p->is_error = 0;
    p->owns_value = 0;
    p->future = NULL;

    rt_obj_set_finalizer(p, promise_finalizer);
    return p;
}

/// @brief Get the Future associated with this Promise (the read-side of the async result).
void *rt_promise_get_future(void *obj) {
    if (!obj)
        rt_trap("Promise: null object");

    promise_impl *p = (promise_impl *)obj;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (!p->future) {
        future_impl *f = (future_impl *)rt_obj_new_i64(0, (int64_t)sizeof(future_impl));
        if (!f) {
#ifdef _WIN32
            LeaveCriticalSection(&p->mutex);
#else
            pthread_mutex_unlock(&p->mutex);
#endif
            rt_trap("Future: memory allocation failed");
        }
        f->promise = p;
        rt_obj_retain_maybe(p); // Future holds a reference to prevent premature GC of promise
        rt_obj_set_finalizer(f, future_finalizer);
        p->future = f;
    }

    void *result = p->future;

#ifdef _WIN32
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

static void future_finalizer(void *obj) {
    future_impl *f = (future_impl *)obj;
    if (!f || !f->promise)
        return;

    promise_impl *p = f->promise;
#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif
    if (p->future == obj)
        p->future = NULL;
#ifdef _WIN32
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_unlock(&p->mutex);
#endif

    if (rt_obj_release_check0(p))
        rt_obj_free(p);
}

/// @brief Set a value in the promise.
void rt_promise_set(void *obj, void *value) {
    if (!obj)
        rt_trap("Promise: null object");

    promise_impl *p = (promise_impl *)obj;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (p->done) {
#ifdef _WIN32
        LeaveCriticalSection(&p->mutex);
#else
        pthread_mutex_unlock(&p->mutex);
#endif
        rt_trap("Promise: already completed");
    }

    p->value = value;
    p->done = 1;
    p->is_error = 0;
    p->owns_value = 0;
    future_listener *listeners = p->listeners;
    p->listeners = NULL;

#ifdef _WIN32
    WakeAllConditionVariable(&p->cond);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->mutex);
#endif

    future_notify_listeners(listeners);
}

void rt_promise_set_owned(void *obj, void *value) {
    if (!obj)
        rt_trap("Promise: null object");

    promise_impl *p = (promise_impl *)obj;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (p->done) {
#ifdef _WIN32
        LeaveCriticalSection(&p->mutex);
#else
        pthread_mutex_unlock(&p->mutex);
#endif
        rt_trap("Promise: already completed");
    }

    if (value)
        rt_obj_retain_maybe(value);
    p->value = value;
    p->done = 1;
    p->is_error = 0;
    p->owns_value = value ? 1 : 0;
    future_listener *listeners = p->listeners;
    p->listeners = NULL;

#ifdef _WIN32
    WakeAllConditionVariable(&p->cond);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->mutex);
#endif

    future_notify_listeners(listeners);
}

/// @brief Complete the promise with an error; wakes all waiting futures.
void rt_promise_set_error(void *obj, rt_string error) {
    if (!obj)
        rt_trap("Promise: null object");

    promise_impl *p = (promise_impl *)obj;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (p->done) {
#ifdef _WIN32
        LeaveCriticalSection(&p->mutex);
#else
        pthread_mutex_unlock(&p->mutex);
#endif
        rt_trap("Promise: already completed");
    }

    // Copy the error string
    const char *err_str = rt_string_cstr(error);
    p->error =
        err_str ? rt_string_from_bytes(err_str, strlen(err_str)) : rt_const_cstr("Unknown error");
    p->done = 1;
    p->is_error = 1;
    p->owns_value = 0;
    future_listener *listeners = p->listeners;
    p->listeners = NULL;

#ifdef _WIN32
    WakeAllConditionVariable(&p->cond);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->mutex);
#endif

    future_notify_listeners(listeners);
}

/// @brief Check whether the promise has been completed (either Ok or Error).
int8_t rt_promise_is_done(void *obj) {
    if (!obj)
        return 0;

    promise_impl *p = (promise_impl *)obj;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    int8_t result = p->done;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t result = p->done;
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

//=============================================================================
// Future Implementation
//=============================================================================

/// @brief Block until the Future has a value and return it (traps if the Future resolved to error).
void *rt_future_get(void *obj) {
    if (!obj)
        rt_trap("Future: null object");

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    while (!p->done) {
        SleepConditionVariableCS(&p->cond, &p->mutex, INFINITE);
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    while (!p->done) {
        pthread_cond_wait(&p->cond, &p->mutex);
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (p->is_error) {
        const char *err = rt_string_cstr(p->error);
        rt_trap(err ? err : "Future: resolved with error");
    }

    return future_export_value_locked(p);
}

/// @brief Wait up to @p ms milliseconds for the future to complete, returning success.
/// @details If the future completes within the timeout and is not an error, stores
///          the result in *out and returns 1. Returns 0 on timeout or error.
int8_t rt_future_get_for(void *obj, int64_t ms, void **out) {
    if (!obj)
        return 0;
    if (ms <= 0)
        return rt_future_try_get(obj, out);

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    DWORD deadline = GetTickCount() + future_deadline_ms_from_now(ms);
    while (!p->done) {
        DWORD now = GetTickCount();
        DWORD remaining = (deadline > now) ? (deadline - now) : 0;
        if (remaining == 0)
            break;
        if (!SleepConditionVariableCS(&p->cond, &p->mutex, remaining) && !p->done) {
            break;
        }
    }
    int8_t success = p->done && !p->is_error;
    if (success && out) {
        *out = future_export_value_locked(p);
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    future_deadline_t deadline = future_deadline_abs_from_now(ms, p->cond_uses_monotonic);
    while (!p->done) {
        int rc =
            future_cond_timedwait_deadline(&p->cond, &p->mutex, deadline, p->cond_uses_monotonic);
        if (rc == ETIMEDOUT && !p->done)
            break;
    }
    int8_t success = p->done && !p->is_error;
    if (success && out) {
        *out = future_export_value_locked(p);
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    return success;
}

/// @brief Check whether the future's underlying promise has been completed.
int8_t rt_future_is_done(void *obj) {
    if (!obj)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    int8_t result = p->done;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t result = p->done;
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

/// @brief Check whether the future completed with an error.
int8_t rt_future_is_error(void *obj) {
    if (!obj)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    int8_t result = p->done && p->is_error;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t result = p->done && p->is_error;
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

/// @brief Return the error message if the future failed, or empty string otherwise.
rt_string rt_future_get_error(void *obj) {
    if (!obj)
        return rt_const_cstr("");

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    rt_string result = rt_const_cstr("");
    if (p->done && p->is_error && p->error)
        result = rt_string_ref(p->error);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    rt_string result = rt_const_cstr("");
    if (p->done && p->is_error && p->error)
        result = rt_string_ref(p->error);
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

/// @brief Get a value from the future.
int8_t rt_future_try_get(void *obj, void **out) {
    if (!obj)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    int8_t success = p->done && !p->is_error;
    if (success && out) {
        *out = future_export_value_locked(p);
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t success = p->done && !p->is_error;
    if (success && out) {
        *out = future_export_value_locked(p);
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    return success;
}

void *rt_future_try_get_val(void *obj) {
    if (!obj)
        return NULL;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;
    void *result = NULL;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    if (p->done && !p->is_error)
        result = future_export_value_locked(p);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (p->done && !p->is_error)
        result = future_export_value_locked(p);
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

void *rt_future_get_for_val(void *obj, int64_t ms) {
    if (!obj)
        return NULL;
    if (ms <= 0)
        return rt_future_try_get_val(obj);

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;
    void *result = NULL;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    DWORD deadline = GetTickCount() + future_deadline_ms_from_now(ms);
    while (!p->done) {
        DWORD now = GetTickCount();
        DWORD remaining = (deadline > now) ? (deadline - now) : 0;
        if (remaining == 0)
            break;
        if (!SleepConditionVariableCS(&p->cond, &p->mutex, remaining) && !p->done) {
            break;
        }
    }
    if (p->done && !p->is_error)
        result = future_export_value_locked(p);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    future_deadline_t deadline = future_deadline_abs_from_now(ms, p->cond_uses_monotonic);
    while (!p->done) {
        int rc =
            future_cond_timedwait_deadline(&p->cond, &p->mutex, deadline, p->cond_uses_monotonic);
        if (rc == ETIMEDOUT && !p->done)
            break;
    }
    if (p->done && !p->is_error)
        result = future_export_value_locked(p);
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

/// @brief Wait the future.
void rt_future_wait(void *obj) {
    if (!obj)
        return;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    while (!p->done) {
        SleepConditionVariableCS(&p->cond, &p->mutex, INFINITE);
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    while (!p->done) {
        pthread_cond_wait(&p->cond, &p->mutex);
    }
    pthread_mutex_unlock(&p->mutex);
#endif
}

/// @brief Block until the future completes or the timeout expires.
/// @details Returns 1 if the future is done (regardless of ok/error), 0 on timeout.
int8_t rt_future_wait_for(void *obj, int64_t ms) {
    if (!obj)
        return 0;
    if (ms <= 0)
        return rt_future_is_done(obj);

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    DWORD deadline = GetTickCount() + future_deadline_ms_from_now(ms);
    while (!p->done) {
        DWORD now = GetTickCount();
        DWORD remaining = (deadline > now) ? (deadline - now) : 0;
        if (remaining == 0)
            break;
        if (!SleepConditionVariableCS(&p->cond, &p->mutex, remaining) && !p->done) {
            break;
        }
    }
    int8_t result = p->done;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    future_deadline_t deadline = future_deadline_abs_from_now(ms, p->cond_uses_monotonic);
    while (!p->done) {
        int rc =
            future_cond_timedwait_deadline(&p->cond, &p->mutex, deadline, p->cond_uses_monotonic);
        if (rc == ETIMEDOUT && !p->done)
            break;
    }
    int8_t result = p->done;
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

int8_t rt_future_on_complete_ex(void *obj,
                                void (*callback)(void *future, void *ctx),
                                void *ctx,
                                void (*cancel)(void *ctx)) {
    if (!obj || !callback)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

    future_listener *listener = (future_listener *)calloc(1, sizeof(future_listener));
    if (!listener)
        return 0;
    listener->callback = callback;
    listener->cancel = cancel;
    listener->ctx = ctx;
    listener->future_obj = obj;
    rt_obj_retain_maybe(obj);

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (p->done) {
#ifdef _WIN32
        LeaveCriticalSection(&p->mutex);
#else
        pthread_mutex_unlock(&p->mutex);
#endif
        callback(obj, ctx);
        if (rt_obj_release_check0(listener->future_obj))
            rt_obj_free(listener->future_obj);
        free(listener);
        return 1;
    }

    future_listener **tail = &p->listeners;
    while (*tail)
        tail = &(*tail)->next;
    *tail = listener;

#ifdef _WIN32
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_unlock(&p->mutex);
#endif

    return 1;
}

int8_t rt_future_on_complete(void *obj, void (*callback)(void *future, void *ctx), void *ctx) {
    return rt_future_on_complete_ex(obj, callback, ctx, NULL);
}

int8_t rt_future_cancel_listener(void *obj, void (*callback)(void *future, void *ctx), void *ctx) {
    if (!obj || !callback)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;
    future_listener *removed = NULL;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    future_listener **link = &p->listeners;
    while (*link) {
        future_listener *cur = *link;
        if (cur->callback == callback && cur->ctx == ctx) {
            *link = cur->next;
            removed = cur;
            break;
        }
        link = &cur->next;
    }

#ifdef _WIN32
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_unlock(&p->mutex);
#endif

    if (!removed)
        return 0;

    if (removed->cancel)
        removed->cancel(removed->ctx);
    if (rt_obj_release_check0(removed->future_obj))
        rt_obj_free(removed->future_obj);
    free(removed);
    return 1;
}

void *rt_future_peek_value(void *obj) {
    if (!obj)
        return NULL;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;
    void *result = NULL;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    if (p->done && !p->is_error)
        result = p->value;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (p->done && !p->is_error)
        result = p->value;
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

int8_t rt_future_value_is_owned(void *obj) {
    if (!obj)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;
    int8_t owned = 0;

#ifdef _WIN32
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

    return owned;
}
