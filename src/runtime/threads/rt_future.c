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

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/time.h>
#endif

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct
{
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
    void *future; // Cached future object
} promise_impl;

typedef struct
{
    promise_impl *promise;
} future_impl;

//=============================================================================
// Promise Implementation
//=============================================================================

void *rt_promise_new(void)
{
    promise_impl *p = (promise_impl *)rt_obj_new_i64(0, (int64_t)sizeof(promise_impl));
    if (!p)
        rt_trap("Promise: memory allocation failed");

#ifdef _WIN32
    InitializeCriticalSection(&p->mutex);
    InitializeConditionVariable(&p->cond);
#else
    pthread_mutex_init(&p->mutex, NULL);
    pthread_cond_init(&p->cond, NULL);
#endif

    p->value = NULL;
    p->error = NULL;
    p->done = 0;
    p->is_error = 0;
    p->future = NULL;

    return p;
}

void *rt_promise_get_future(void *obj)
{
    if (!obj)
        rt_trap("Promise: null object");

    promise_impl *p = (promise_impl *)obj;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (!p->future)
    {
        future_impl *f = (future_impl *)rt_obj_new_i64(0, (int64_t)sizeof(future_impl));
        if (!f)
        {
#ifdef _WIN32
            LeaveCriticalSection(&p->mutex);
#else
            pthread_mutex_unlock(&p->mutex);
#endif
            rt_trap("Future: memory allocation failed");
        }
        f->promise = p;
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

void rt_promise_set(void *obj, void *value)
{
    if (!obj)
        rt_trap("Promise: null object");

    promise_impl *p = (promise_impl *)obj;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (p->done)
    {
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

#ifdef _WIN32
    WakeAllConditionVariable(&p->cond);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->mutex);
#endif
}

void rt_promise_set_error(void *obj, rt_string error)
{
    if (!obj)
        rt_trap("Promise: null object");

    promise_impl *p = (promise_impl *)obj;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
#endif

    if (p->done)
    {
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

#ifdef _WIN32
    WakeAllConditionVariable(&p->cond);
    LeaveCriticalSection(&p->mutex);
#else
    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->mutex);
#endif
}

int8_t rt_promise_is_done(void *obj)
{
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

void *rt_future_get(void *obj)
{
    if (!obj)
        rt_trap("Future: null object");

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    while (!p->done)
    {
        SleepConditionVariableCS(&p->cond, &p->mutex, INFINITE);
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    while (!p->done)
    {
        pthread_cond_wait(&p->cond, &p->mutex);
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    if (p->is_error)
    {
        const char *err = rt_string_cstr(p->error);
        rt_trap(err ? err : "Future: resolved with error");
    }

    return p->value;
}

int8_t rt_future_get_for(void *obj, int64_t ms, void **out)
{
    if (!obj)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    if (!p->done)
    {
        SleepConditionVariableCS(&p->cond, &p->mutex, (DWORD)ms);
    }
    int8_t success = p->done && !p->is_error;
    if (success && out)
    {
        *out = p->value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (!p->done)
    {
        struct timespec ts;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec + ms / 1000;
        ts.tv_nsec = (tv.tv_usec + (ms % 1000) * 1000) * 1000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&p->cond, &p->mutex, &ts);
    }
    int8_t success = p->done && !p->is_error;
    if (success && out)
    {
        *out = p->value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    return success;
}

int8_t rt_future_is_done(void *obj)
{
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

int8_t rt_future_is_error(void *obj)
{
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

rt_string rt_future_get_error(void *obj)
{
    if (!obj)
        return rt_const_cstr("");

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    rt_string result = (p->done && p->is_error) ? p->error : rt_const_cstr("");
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    rt_string result = (p->done && p->is_error) ? p->error : rt_const_cstr("");
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

int8_t rt_future_try_get(void *obj, void **out)
{
    if (!obj)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    int8_t success = p->done && !p->is_error;
    if (success && out)
    {
        *out = p->value;
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    int8_t success = p->done && !p->is_error;
    if (success && out)
    {
        *out = p->value;
    }
    pthread_mutex_unlock(&p->mutex);
#endif

    return success;
}

void *rt_future_try_get_val(void *obj)
{
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

void *rt_future_get_for_val(void *obj, int64_t ms)
{
    if (!obj)
        return NULL;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;
    void *result = NULL;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    if (!p->done)
    {
        SleepConditionVariableCS(&p->cond, &p->mutex, (DWORD)ms);
    }
    if (p->done && !p->is_error)
        result = p->value;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (!p->done)
    {
        struct timespec ts;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec + ms / 1000;
        ts.tv_nsec = (tv.tv_usec + (ms % 1000) * 1000) * 1000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&p->cond, &p->mutex, &ts);
    }
    if (p->done && !p->is_error)
        result = p->value;
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}

void rt_future_wait(void *obj)
{
    if (!obj)
        return;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    while (!p->done)
    {
        SleepConditionVariableCS(&p->cond, &p->mutex, INFINITE);
    }
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    while (!p->done)
    {
        pthread_cond_wait(&p->cond, &p->mutex);
    }
    pthread_mutex_unlock(&p->mutex);
#endif
}

int8_t rt_future_wait_for(void *obj, int64_t ms)
{
    if (!obj)
        return 0;

    future_impl *f = (future_impl *)obj;
    promise_impl *p = f->promise;

#ifdef _WIN32
    EnterCriticalSection(&p->mutex);
    if (!p->done)
    {
        SleepConditionVariableCS(&p->cond, &p->mutex, (DWORD)ms);
    }
    int8_t result = p->done;
    LeaveCriticalSection(&p->mutex);
#else
    pthread_mutex_lock(&p->mutex);
    if (!p->done)
    {
        struct timespec ts;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec + ms / 1000;
        ts.tv_nsec = (tv.tv_usec + (ms % 1000) * 1000) * 1000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&p->cond, &p->mutex, &ts);
    }
    int8_t result = p->done;
    pthread_mutex_unlock(&p->mutex);
#endif

    return result;
}
