// File: tests/runtime/RTAllocZeroTests.cpp
// Purpose: Verify rt_alloc handles zero-byte requests without trapping.
// Key invariants: Under glibc, malloc(0) is forced to return NULL so legacy
//                 behavior would trap. The test asserts the new logic treats
//                 zero as a valid allocation size.
// Ownership: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
#include "rt.hpp"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#if defined(__GLIBC__)
extern "C" void *__libc_malloc(size_t size);

extern "C" void *malloc(size_t size)
{
    if (size == 0)
        return NULL;
    return __libc_malloc(size);
}
#endif

int main()
{
    void *ptr = rt_alloc(0);
    assert(ptr != NULL);
    free(ptr);
    return 0;
}
