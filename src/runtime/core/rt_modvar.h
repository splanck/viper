//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_modvar.h
// Purpose: Runtime support for module-level (global) BASIC variables, providing stable address lookup by name and type for i64, f64, i1, pointer, and string variables.
//
// Key invariants:
//   - The same (name, type) pair always yields the same stable address.
//   - Addresses are allocated once per name+type combination and never moved.
//   - Variable names must be non-empty; duplicate registrations return the existing address.
//   - String variables store rt_string pointers; the slot is initialized to NULL.
//
// Ownership/Lifetime:
//   - Storage is allocated once and owned by the runtime for the process lifetime.
//   - Freed at process exit; callers must not free the returned pointers.
//
// Links: src/runtime/core/rt_modvar.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Get the address of a 64-bit integer module variable.
    /// @details Provides stable storage for BASIC global variables. Looks up or
    ///          creates a slot keyed by name and returns its address. The returned
    ///          pointer remains valid for the lifetime of the process.
    /// @param name Runtime string containing the variable name.
    /// @return Pointer to the module variable's storage (never NULL).
    void *rt_modvar_addr_i64(rt_string name);

    /// @brief Get the address of a 64-bit floating-point module variable.
    /// @details Provides stable storage for BASIC global variables. Looks up or
    ///          creates a slot keyed by name and returns its address.
    /// @param name Runtime string containing the variable name.
    /// @return Pointer to the module variable's storage (never NULL).
    void *rt_modvar_addr_f64(rt_string name);

    /// @brief Get the address of a boolean (i1) module variable.
    /// @details Provides stable storage for BASIC global variables. Looks up or
    ///          creates a slot keyed by name and returns its address.
    /// @param name Runtime string containing the variable name.
    /// @return Pointer to the module variable's storage (never NULL).
    void *rt_modvar_addr_i1(rt_string name);

    /// @brief Get the address of a pointer module variable.
    /// @details Provides stable storage for BASIC global variables. Looks up or
    ///          creates a slot keyed by name and returns its address.
    /// @param name Runtime string containing the variable name.
    /// @return Pointer to the module variable's storage (never NULL).
    void *rt_modvar_addr_ptr(rt_string name);

    /// @brief Get the address of a string module variable.
    /// @details Provides stable storage for BASIC global variables. Looks up or
    ///          creates a slot keyed by name and returns its address.
    /// @param name Runtime string containing the variable name.
    /// @return Pointer to the module variable's storage (never NULL).
    void *rt_modvar_addr_str(rt_string name);

    /// @brief Get the address of a module variable block with arbitrary size.
    /// @details Supports arrays and records as module-level variables. Looks up
    ///          or creates a slot keyed by name and returns its address. The
    ///          allocated block is zero-initialized on first creation.
    /// @param name Runtime string containing the variable name.
    /// @param size Size of the variable block in bytes.
    /// @return Pointer to the module variable's storage (never NULL).
    void *rt_modvar_addr_block(rt_string name, int64_t size);

#ifdef __cplusplus
}
#endif
