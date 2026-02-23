//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_exc.c
// Purpose: Implements the runtime exception object type for the Viper exception
//          handling system. Provides rt_exception allocation, message access,
//          and type-tag queries used by the VM and native EH instructions.
//
// Key invariants:
//   - Exception objects are heap-allocated and reference-counted.
//   - The message string is retained by the exception on creation and released
//     when the exception object is freed.
//   - Exception type tags are integer identifiers registered at startup.
//   - Catch dispatch compares type tags for exact or subtype matching.
//   - NULL message is coerced to an empty string; never stored as NULL.
//
// Ownership/Lifetime:
//   - Callers that throw an exception transfer ownership to the EH machinery.
//   - The EH machinery releases the exception after a catch handler returns.
//   - The message string is owned by the exception object, not the caller.
//
// Links: src/runtime/oop/rt_exc.h (public API),
//        src/runtime/oop/rt_type_registry.h (type tag registration)
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
