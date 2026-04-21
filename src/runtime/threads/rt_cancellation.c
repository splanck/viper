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
//   - Local cancellation state can be reset for reuse via rt_cancellation_reset.
//   - Linked parent tokens propagate cancellation down to children on cancel.
//   - IsCancelled is always safe to call from any thread without locking.
//   - The finalizer releases any retained linked parent token.
//
// Ownership/Lifetime:
//   - Cancellation token objects are heap-allocated and managed by the GC.
//   - Linked child tokens retain their parent token until finalization.
//
// Links: src/runtime/threads/rt_cancellation.h (public API),
//        src/runtime/threads/rt_future.h (futures accept a cancellation token),
//        src/runtime/threads/rt_async.h (async tasks use cancellation tokens)
//
//===----------------------------------------------------------------------===//

#include "rt_cancellation.h"

#include "rt_error.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_threads.h"

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

// --- Internal structures ---

typedef struct rt_cancellation_data {
#if defined(_WIN32)
    volatile LONG cancelled;
#else
    atomic_int cancelled;
#endif
    void *parent;                         // linked parent token (NULL if root)
    void *monitor;                        // protects the weak child list
    struct rt_cancellation_data *first_child;
    struct rt_cancellation_data *next_sibling;
} rt_cancellation_data;

// --- Platform-specific atomic helpers ---

static inline void cancel_init(rt_cancellation_data *data, int value) {
#if defined(_WIN32)
    InterlockedExchange(&data->cancelled, (LONG)value);
#else
    atomic_init(&data->cancelled, value);
#endif
}

static inline int cancel_load(rt_cancellation_data *data) {
#if defined(_WIN32)
    return (int)InterlockedCompareExchange(&data->cancelled, 0, 0);
#else
    return atomic_load(&data->cancelled);
#endif
}

static inline void cancel_store(rt_cancellation_data *data, int value) {
#if defined(_WIN32)
    InterlockedExchange(&data->cancelled, (LONG)value);
#else
    atomic_store(&data->cancelled, value);
#endif
}

static void cancellation_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void cancellation_mark_cancelled(rt_cancellation_data *data) {
    if (!data)
        return;
    cancel_store(data, 1);
    if (!data->monitor)
        return;

    rt_monitor_enter(data->monitor);
    for (rt_cancellation_data *child = data->first_child; child; child = child->next_sibling)
        cancellation_mark_cancelled(child);
    rt_monitor_exit(data->monitor);
}

static void cancellation_finalizer(void *obj) {
    rt_cancellation_data *data = (rt_cancellation_data *)obj;
    if (!data)
        return;

    if (data->parent) {
        rt_cancellation_data *parent = (rt_cancellation_data *)data->parent;
        if (parent->monitor) {
            rt_monitor_enter(parent->monitor);
            rt_cancellation_data **link = &parent->first_child;
            while (*link) {
                if (*link == data) {
                    *link = data->next_sibling;
                    break;
                }
                link = &(*link)->next_sibling;
            }
            rt_monitor_exit(parent->monitor);
        }
        cancellation_release_object(data->parent);
        data->parent = NULL;
    }

    data->first_child = NULL;
    data->next_sibling = NULL;

    if (data->monitor) {
        cancellation_release_object(data->monitor);
        data->monitor = NULL;
    }
}

// --- Public API ---

/// @brief Create a new cancellation token (initially not cancelled).
void *rt_cancellation_new(void) {
    void *obj = rt_obj_new_i64(0, sizeof(rt_cancellation_data));
    if (!obj) {
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "CancelToken.New: memory allocation failed");
        return NULL;
    }
    rt_cancellation_data *data = (rt_cancellation_data *)obj;
    cancel_init(data, 0);
    data->parent = NULL;
    data->monitor = rt_obj_new_i64(0, 1);
    if (!data->monitor) {
        cancellation_release_object(obj);
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "CancelToken.New: memory allocation failed");
        return NULL;
    }
    data->first_child = NULL;
    data->next_sibling = NULL;
    rt_obj_set_finalizer(obj, cancellation_finalizer);
    return obj;
}

/// @brief Check whether cancellation has been requested on this token.
int8_t rt_cancellation_is_cancelled(void *token) {
    if (!token)
        return 0;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    if (cancel_load(data))
        return 1;
    if (data->parent)
        return rt_cancellation_is_cancelled(data->parent);
    return 0;
}

/// @brief Request cancellation on this token, propagating to all child tokens.
void rt_cancellation_cancel(void *token) {
    if (!token)
        return;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    cancellation_mark_cancelled(data);
}

/// @brief Set a value in the cancellation.
void rt_cancellation_reset(void *token) {
    if (!token)
        return;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    cancel_store(data, 0);
}

/// @brief Create a child token that is automatically cancelled when the parent is cancelled.
void *rt_cancellation_linked(void *parent) {
    void *obj = rt_obj_new_i64(0, sizeof(rt_cancellation_data));
    if (!obj) {
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "CancelToken.Linked: memory allocation failed");
        return NULL;
    }
    rt_cancellation_data *data = (rt_cancellation_data *)obj;
    cancel_init(data, 0);
    data->parent = NULL;
    data->monitor = rt_obj_new_i64(0, 1);
    if (!data->monitor) {
        cancellation_release_object(obj);
        rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR,
                           Err_RuntimeError,
                           -1,
                           "CancelToken.Linked: memory allocation failed");
        return NULL;
    }
    data->first_child = NULL;
    data->next_sibling = NULL;

    if (parent) {
        rt_obj_retain_maybe(parent);
        data->parent = parent;
        rt_cancellation_data *parent_data = (rt_cancellation_data *)parent;
        if (rt_cancellation_is_cancelled(parent))
            cancel_store(data, 1);
        if (parent_data->monitor) {
            rt_monitor_enter(parent_data->monitor);
            data->next_sibling = parent_data->first_child;
            parent_data->first_child = data;
            rt_monitor_exit(parent_data->monitor);
        }
    }
    rt_obj_set_finalizer(obj, cancellation_finalizer);
    return obj;
}

/// @brief Check the cancellation.
int8_t rt_cancellation_check(void *token) {
    return rt_cancellation_is_cancelled(token);
}

/// @brief Throw the if cancelled of the cancellation.
void rt_cancellation_throw_if_cancelled(void *token) {
    if (rt_cancellation_check(token))
        rt_trap_raise_kind(RT_TRAP_KIND_INTERRUPT,
                           0,
                           -1,
                           "OperationCancelledException: cancellation was requested");
}
