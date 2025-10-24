// File: src/runtime/rt_debug.c
// Purpose: Implements debug printing helpers for deterministic IL tests.
// Key invariants: Functions flush stdout to ensure immediate visibility.
// Ownership/Lifetime: Borrowed strings remain owned by the caller.
// Links: docs/codemap.md

#include "rt_debug.h"

#include <stdio.h>

void rt_println_i32(int32_t value)
{
    printf("%d\n", value);
    fflush(stdout);
}

void rt_println_str(const char *text)
{
    if (!text)
        text = "";
    printf("%s\n", text);
    fflush(stdout);
}
