//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: runtime/rt_exc.c
// Purpose: Runtime exception support implementation.
// Key invariants: Exception objects follow ref-counting rules.
// Ownership/Lifetime: Message string is retained by exception, released on free.
//
//===----------------------------------------------------------------------===//

#include "rt_exc.h"
#include "rt_object.h"
#include "rt_string.h"
#include <stdlib.h>

/// @brief Internal exception object structure.
/// Layout: [vtable_ptr (8 bytes)][message (8 bytes)]
typedef struct
{
    void *vtable;      ///< Vtable pointer (not used for Exception but reserved)
    rt_string message; ///< Exception message
} rt_exception_t;

/// @brief Finalizer for exception objects - releases the message string.
static void exception_finalizer(void *obj)
{
    rt_exception_t *exc = (rt_exception_t *)obj;
    if (exc->message)
    {
        rt_str_release_maybe(exc->message);
    }
}

void *rt_exc_create(rt_string msg)
{
    // Allocate exception object with class ID 1 (Exception)
    rt_exception_t *exc =
        (rt_exception_t *)rt_obj_new_i64(RT_EXCEPTION_CLASS_ID, sizeof(rt_exception_t));
    if (!exc)
        return NULL;

    // Initialize vtable pointer to NULL (Exception is a simple class)
    exc->vtable = NULL;

    // Store and retain the message
    exc->message = msg;
    if (msg)
    {
        rt_str_retain_maybe(msg);
    }

    // Set finalizer to release message when exception is freed
    rt_obj_set_finalizer(exc, exception_finalizer);

    return exc;
}

rt_string rt_exc_get_message(void *exc)
{
    if (!exc)
        return NULL;

    rt_exception_t *e = (rt_exception_t *)exc;
    return e->message;
}

int64_t rt_exc_is_exception(void *obj)
{
    // For now, just check if it's a valid pointer
    // In a full implementation, we'd check the class ID
    return obj != NULL ? 1 : 0;
}
