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
// Ownership/Lifetime: Allocated once per name+type; freed at process exit.

#pragma once

#include "rt_string.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Return addresses to runtime-managed storage for module-level variables.
    /// What: Address of a 64-bit integer module variable named @p name.
    /// Why:  Provide stable storage for BASIC global variables.
    /// How:  Looks up/creates a slot keyed by name and returns its address.
    void *rt_modvar_addr_i64(rt_string name);

    /// What: Address of a 64-bit floating module variable named @p name.
    /// Why:  Provide stable storage for BASIC global variables.
    /// How:  Looks up/creates a slot keyed by name and returns its address.
    void *rt_modvar_addr_f64(rt_string name);

    /// What: Address of a boolean (i1) module variable named @p name.
    /// Why:  Provide stable storage for BASIC global variables.
    /// How:  Looks up/creates a slot keyed by name and returns its address.
    void *rt_modvar_addr_i1(rt_string name);

    /// What: Address of a pointer module variable named @p name.
    /// Why:  Provide stable storage for BASIC global variables.
    /// How:  Looks up/creates a slot keyed by name and returns its address.
    void *rt_modvar_addr_ptr(rt_string name);

    /// What: Address of a string module variable named @p name.
    /// Why:  Provide stable storage for BASIC global variables.
    /// How:  Looks up/creates a slot keyed by name and returns its address.
    void *rt_modvar_addr_str(rt_string name);

#ifdef __cplusplus
}
#endif
