//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_cancellation.h"

#include "rt_internal.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

// --- Internal structures ---

typedef struct
{
    atomic_int cancelled;
    void *parent; // linked parent token (NULL if root)
} rt_cancellation_data;

static void cancellation_finalizer(void *obj)
{
    (void)obj;
    // No dynamic allocations to free
}

// --- Public API ---

void *rt_cancellation_new(void)
{
    void *obj = rt_obj_new_i64(0, sizeof(rt_cancellation_data));
    rt_cancellation_data *data = (rt_cancellation_data *)obj;
    atomic_init(&data->cancelled, 0);
    data->parent = NULL;
    rt_obj_set_finalizer(obj, cancellation_finalizer);
    return obj;
}

int8_t rt_cancellation_is_cancelled(void *token)
{
    if (!token)
        return 0;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    return atomic_load(&data->cancelled) ? 1 : 0;
}

void rt_cancellation_cancel(void *token)
{
    if (!token)
        return;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    atomic_store(&data->cancelled, 1);
}

void rt_cancellation_reset(void *token)
{
    if (!token)
        return;
    rt_cancellation_data *data = (rt_cancellation_data *)token;
    atomic_store(&data->cancelled, 0);
}

void *rt_cancellation_linked(void *parent)
{
    void *obj = rt_obj_new_i64(0, sizeof(rt_cancellation_data));
    rt_cancellation_data *data = (rt_cancellation_data *)obj;
    atomic_init(&data->cancelled, 0);
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
    if (atomic_load(&data->cancelled))
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
