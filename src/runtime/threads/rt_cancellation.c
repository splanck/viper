//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_cancellation.c
// Purpose: Implements a cooperative cancellation token for the
//          Viper.Threads.Cancellation class. Tokens can be cancelled, checked,
//          and linked in a parent-child hierarchy so that cancelling a parent
//          propagates to all linked child tokens.
//
// Key invariants:
//   - Cancellation state is stored as an atomic_int (POSIX) or volatile LONG
//     (Win32) to allow lock-free reads from any thread.
//   - A token can only transition from not-cancelled to cancelled, never back.
//   - Linked parent tokens propagate cancellation down to children on cancel.
//   - IsCancelled is always safe to call from any thread without locking.
//   - The finalizer is a no-op; atomic fields require no special cleanup.
//
// Ownership/Lifetime:
//   - Cancellation token objects are heap-allocated and managed by the GC.
//   - Parent pointers are weak references; the parent is not retained by the child.
//
// Links: src/runtime/threads/rt_cancellation.h (public API),
//        src/runtime/threads/rt_future.h (futures accept a cancellation token),
//        src/runtime/threads/rt_async.h (async tasks use cancellation tokens)
//
//===----------------------------------------------------------------------===//

#include "rt_cancellation.h"

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <stdatomic.h>
#endif

extern void rt_trap(const char *msg);

// --- Internal structures ---

typedef struct
{
#if defined(_WIN32)
    volatile LONG cancelled;
#else
    atomic_int cancelled;
#endif
    void *parent; // linked parent token (NULL if root)
} rt_cancellation_data;

static void cancellation_finalizer(void *obj)
{
    (void)obj;
    // No dynamic allocations to free
}

// --- Platform-specific atomic helpers ---

static inline void cancel_init(rt_cancellation_data *data, int value)
{
#if defined(_WIN32)
    InterlockedExchange(&data->cancelled, (LONG)value);
#else
    atomic_init(&data->cancelled, value);
#endif
}

static inline int cancel_load(rt_cancellation_data *data)
{
#if defined(_WIN32)
    return (int)InterlockedCompareExchange(&data->cancelled, 0, 0);
#else
    return atomic_load(&data->cancelled);
#endif
}

static inline void cancel_store(rt_cancellation_data *data, int value)
{
#if defined(_WIN32)
    InterlockedExchange(&data->cancelled, (LONG)value);
#else
    atomic_store(&data->cancelled, value);
#endif
}

// --- Public API ---

void *rt_cancellation_new(void)
{
    void *obj = rt_obj_new_i64(0, sizeof(rt_cancellation_data));
    rt_cancellation_data *data = (rt_cancellation_data *)obj;
    cancel_init(data, 0);
    data->parent = NULL;
    rt_obj_set_finalizer(obj, cancellation_finalizer);
    return obj;
}

int8_t rt_cancellation_is_cancelled(void *token)
{
    if (!token)
        return 0;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    return cancel_load(data) ? 1 : 0;
}

void rt_cancellation_cancel(void *token)
{
    if (!token)
        return;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    cancel_store(data, 1);
}

void rt_cancellation_reset(void *token)
{
    if (!token)
        return;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    cancel_store(data, 0);
}

void *rt_cancellation_linked(void *parent)
{
    void *obj = rt_obj_new_i64(0, sizeof(rt_cancellation_data));
    rt_cancellation_data *data = (rt_cancellation_data *)obj;
    cancel_init(data, 0);
    data->parent = parent;
    if (parent)
        rt_obj_retain_maybe(parent);
    rt_obj_set_finalizer(obj, cancellation_finalizer);
    return obj;
}

int8_t rt_cancellation_check(void *token)
{
    if (!token)
        return 0;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    if (cancel_load(data))
        return 1;
    if (data->parent)
        return rt_cancellation_is_cancelled(data->parent);
    return 0;
}

void rt_cancellation_throw_if_cancelled(void *token)
{
    if (rt_cancellation_check(token))
        rt_trap("OperationCancelledException: cancellation was requested");
}
