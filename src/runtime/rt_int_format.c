// File: src/runtime/rt_int_format.c
// Purpose: Implements portable helpers for formatting 64-bit integers for the BASIC runtime.
// Key invariants: Uses C-locale formatting and always null-terminates on success.
// Ownership/Lifetime: Callers provide and own the destination buffers.
// Links: docs/codemap.md

#include "rt_int_format.h"

#include <inttypes.h>
#include <stdio.h>

size_t rt_i64_to_cstr(int64_t value, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0)
        return 0;
    int written = snprintf(buffer, capacity, "%" PRId64, (int64_t)value);
    if (written < 0)
    {
        buffer[0] = '\0';
        return 0;
    }
    if ((size_t)written >= capacity)
    {
        buffer[capacity - 1] = '\0';
        return capacity - 1;
    }
    return (size_t)written;
}

size_t rt_u64_to_cstr(uint64_t value, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0)
        return 0;
    int written = snprintf(buffer, capacity, "%" PRIu64, (uint64_t)value);
    if (written < 0)
    {
        buffer[0] = '\0';
        return 0;
    }
    if ((size_t)written >= capacity)
    {
        buffer[capacity - 1] = '\0';
        return capacity - 1;
    }
    return (size_t)written;
}
