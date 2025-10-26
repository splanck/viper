// File: src/runtime/rt_trap.h
// Purpose: Declares runtime trap helpers for fatal error conditions.
// Key invariants: Trap helpers terminate the process immediately after reporting.
// Ownership/Lifetime: Functions only; no resource ownership.
// Links: docs/codemap.md

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Traps the runtime on division by zero.
void rt_trap_div0(void);

#ifdef __cplusplus
}
#endif
