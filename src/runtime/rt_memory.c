//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

/// @brief Allocate zero-initialised storage for runtime subsystems.
/// @details Performs a sequence of validation steps before delegating to
///          @c calloc:
///          - Rejects negative requests and values that do not fit into
///            @c size_t so the platform allocator never observes undefined
///            behaviour.
///          - Promotes zero-byte requests to a single byte so callers never
///            receive @c NULL on success.
///          - Emits a runtime trap when the platform allocator fails, matching
///            the VM's fatal error semantics.
/// @param bytes Number of bytes requested by the caller.
/// @return Pointer to zeroed storage on success; @c NULL after reporting a trap
///         when the allocation fails.
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
