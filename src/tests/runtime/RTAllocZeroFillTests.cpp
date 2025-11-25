//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTAllocZeroFillTests.cpp
// Purpose: Verify rt_alloc returns zero-initialized memory.
// Key invariants: Memory returned from rt_alloc must contain only zero bytes.
// Ownership/Lifetime: Uses runtime library and frees allocated memory.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GLIBC__)
extern "C" void *__libc_malloc(size_t size);

extern "C" void *malloc(size_t size)
{
    void *ptr = __libc_malloc(size);
    if (ptr)
        memset(ptr, 0xAB, size);
    return ptr;
}
#endif

int main()
{
    const size_t size = 64;
    uint8_t *bytes = (uint8_t *)rt_alloc((int64_t)size);
    assert(bytes != NULL);
    for (size_t i = 0; i < size; ++i)
        assert(bytes[i] == 0);
    free(bytes);
    return 0;
}
