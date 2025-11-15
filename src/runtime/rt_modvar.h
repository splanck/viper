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
    void *rt_modvar_addr_i64(rt_string name);
    void *rt_modvar_addr_f64(rt_string name);
    void *rt_modvar_addr_i1(rt_string name);
    void *rt_modvar_addr_ptr(rt_string name);
    void *rt_modvar_addr_str(rt_string name);

#ifdef __cplusplus
}
#endif
