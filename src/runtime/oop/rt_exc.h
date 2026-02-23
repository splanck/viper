//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_exc.h
// Purpose: Runtime exception support for structured exception handling, providing exception object creation, message access, and type checking for catch handlers.
//
// Key invariants:
//   - Exception objects carry a class ID for type-based catch dispatch.
//   - Exception objects are reference-counted and contain a message string.
//   - rt_exc_class_id returns the class ID for use in EhEntry IL opcodes.
//   - Exception objects follow normal refcount rules; callers must balance retain/release.
//
// Ownership/Lifetime:
//   - Exception objects start with refcount 1; caller owns the initial reference.
//   - Message strings are retained by the exception object.
//
// Links: src/runtime/oop/rt_exc.c (implementation), src/runtime/core/rt_string.h
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
