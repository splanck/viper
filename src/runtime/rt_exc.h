//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: runtime/rt_exc.h
// Purpose: Runtime exception support for Pascal exception handling.
// Key invariants: Exception objects are ref-counted and contain a message string.
// Ownership/Lifetime: Exception objects follow normal ref-counting rules.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Exception class ID for type checking in exception handlers.
    /// This is a well-known class ID for the built-in Exception class.
#define RT_EXCEPTION_CLASS_ID 1

    /// @brief Create a new Exception object with the given message.
    /// @param msg The exception message (string pointer).
    /// @return Pointer to the newly allocated Exception object.
    void *rt_exc_create(rt_string msg);

    /// @brief Get the message from an Exception object.
    /// @param exc Pointer to an Exception object.
    /// @return The exception message string.
    rt_string rt_exc_get_message(void *exc);

    /// @brief Check if an object is an Exception or derives from it.
    /// @param obj Pointer to an object.
    /// @return 1 if the object is an Exception, 0 otherwise.
    int64_t rt_exc_is_exception(void *obj);

#ifdef __cplusplus
}
#endif
