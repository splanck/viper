// File: src/runtime/rt_memory.c
// Purpose: Implements memory allocation helpers for the BASIC runtime.
// Key invariants: Allocation sizes must be non-negative and fit in size_t.
// Ownership/Lifetime: Caller owns and must free returned memory blocks.
// Links: docs/codemap.md

#include "rt_internal.h"
#include <stdlib.h>

/**
 * Allocate a block of memory for runtime usage.
 *
 * @param bytes Number of bytes to allocate. Must be non-negative.
 *              A zero-byte request yields a one-byte allocation to ensure a
 *              non-null result.
 * @return Pointer to allocated memory, or traps on invalid input or failure.
 */
void *rt_alloc(int64_t bytes)
{
    if (bytes < 0)
        return rt_trap("negative allocation"), NULL;
    if ((uint64_t)bytes > SIZE_MAX)
    {
        rt_trap("allocation too large");
        return NULL;
    }
    size_t request = (size_t)bytes;
    if (request == 0)
        request = 1;
    void *p = calloc(1, request);
    if (!p)
        rt_trap("out of memory");
    return p;
}
