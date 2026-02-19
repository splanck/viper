//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime's heap allocation shim.  The helper validates
// requested sizes, enforces non-negative limits, and guarantees that callers
// receive zero-initialised buffers even when they request zero bytes.  The
// routine intentionally mirrors the VM's allocation behaviour so diagnostics and
// trap semantics remain aligned between native and interpreted execution.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Runtime heap allocation shim shared by BASIC intrinsics.
/// @details Provides @ref rt_alloc, a defensive wrapper around @c calloc that
///          clamps negative or oversized requests and reports fatal errors via
///          @ref rt_trap.  The implementation centralises allocation policy so
///          higher-level runtime components can assume successful calls return
///          zeroed storage of at least one byte.

#include "rt_internal.h"
#include <stdlib.h>

static void *rt_alloc_impl(int64_t bytes)
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

static rt_alloc_hook_fn g_rt_alloc_hook = NULL;

/// @brief Install a hook that can override @ref rt_alloc for testing.
/// @details The hook receives the requested byte count along with a pointer to
///          the default implementation.  Passing @c NULL restores the default
///          behaviour.  Intended for unit tests that need to simulate allocator
///          failures without exhausting system memory.
/// @param hook Replacement function or @c NULL to disable overrides.
void rt_set_alloc_hook(rt_alloc_hook_fn hook)
{
    g_rt_alloc_hook = hook;
}

/// @brief Allocate zero-initialised storage for runtime subsystems.
/// @details Delegates to the optional test hook when installed, otherwise calls
///          the default implementation described above.
/// @param bytes Number of bytes requested by the caller.
/// @return Pointer to zeroed storage on success; @c NULL after reporting a trap
///         when the allocation fails.
void *rt_alloc(int64_t bytes)
{
    if (g_rt_alloc_hook)
        return g_rt_alloc_hook(bytes, rt_alloc_impl);
    return rt_alloc_impl(bytes);
}
