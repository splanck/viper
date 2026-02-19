//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_modvar.h
// Purpose: Runtime support for module-level (global) BASIC variables.
// Key invariants: Returns stable addresses per variable name and type.
//                 The same (name, type) pair always yields the same address.
// Ownership: Allocated once per name+type; owned by the runtime.
// Lifetime: Freed at process exit.
// Links: rt_string.h
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
