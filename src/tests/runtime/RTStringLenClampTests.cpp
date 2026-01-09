//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStringLenClampTests.cpp
// Purpose: Ensure rt_len clamps extremely large heap string lengths.
// Key invariants: rt_len never overflows int64_t even when heap headers are corrupted.
// Ownership/Lifetime: Test allocates and releases its own runtime string handle.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "viper/runtime/rt.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>

int main(void)
{
    // Use a string longer than RT_SSO_MAX_LEN (32) to ensure heap allocation
    static const char *long_str =
        "this_string_is_long_enough_to_bypass_small_string_optimization_and_use_heap";
    const size_t long_len = 76;

    rt_string fabricated = rt_string_from_bytes(long_str, long_len);
    assert(fabricated);

    // Verify string is heap-backed (not SSO)
    assert(fabricated->heap != NULL && fabricated->heap != RT_SSO_SENTINEL);

    rt_heap_hdr_t *hdr = rt_heap_hdr(fabricated->data);
    assert(hdr);

#if SIZE_MAX > INT64_MAX
    hdr->len = (size_t)INT64_MAX + 17;
    int64_t reported = rt_len(fabricated);
    assert(reported == INT64_MAX);
#else
    hdr->len = long_len;
    int64_t reported = rt_len(fabricated);
    assert(reported == (int64_t)long_len);
#endif

    rt_string_unref(fabricated);
    return 0;
}
