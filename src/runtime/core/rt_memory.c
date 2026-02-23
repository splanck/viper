//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_memory.c
// Purpose: Heap allocation shim for the Viper runtime C ABI. Validates
//   requested sizes, enforces non-negative limits, and guarantees that callers
//   receive zero-initialised buffers even for zero-byte requests. Mirrors the
//   VM's allocation semantics so that diagnostics and trap conditions remain
//   consistent between interpreted (VM) and native (AOT) execution paths.
//
// Key invariants:
//   - rt_alloc(n_bytes) always returns a zero-initialised buffer. Callers must
//     not assume undefined content in freshly allocated memory.
//   - Requesting a negative or overflow-inducing size fires rt_trap() rather
//     than returning NULL, keeping error handling uniform with other runtime
//     limit violations.
//   - rt_free(ptr) is a thin wrapper around free(). Passing NULL is safe (no-op,
//     matching standard C free() semantics).
//   - All allocations go through this shim (not direct malloc) so that future
//     allocator instrumentation or custom allocators can be plugged in at a
//     single point.
//
// Ownership/Lifetime:
//   - No internal state. All functions are stateless wrappers. Callers own
//     the returned memory and must free it via rt_free() or rt_obj_free().
//
// Links: src/runtime/core/rt_memory.h (public API),
//        src/runtime/core/rt_trap.h (rt_trap for invalid sizes)
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
