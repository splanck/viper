// File: src/runtime/rt_trap.c
// Purpose: Implements runtime trap helpers for fatal error conditions.
// Key invariants: Trap helpers always terminate the process after reporting the trap reason.
// Ownership/Lifetime: No dynamic resources; delegates to C standard library for I/O and exit.
// Links: docs/codemap.md

#include <stdio.h>
#include <stdlib.h>

#include "rt_trap.h"

/// @brief Trap implementation for division by zero errors.
void rt_trap_div0(void) {
    fprintf(stderr, "Viper runtime trap: division by zero\n");
    fflush(stderr);
    exit(1); // Match VM behavior if your VM uses a specific code; adjust here later if needed.
}
