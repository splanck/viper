// File: tests/runtime/RTStringLenClampTests.cpp
// Purpose: Ensure rt_len clamps extremely large heap string lengths.
// Key invariants: rt_len never overflows int64_t even when heap headers are corrupted.
// Ownership/Lifetime: Test allocates and releases its own runtime string handle.
// Links: docs/codemap.md

#include "rt_internal.h"
#include "rt_string.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>

int main(void)
{
    rt_string fabricated = rt_string_from_bytes("clamp", 5);
    assert(fabricated);

    rt_heap_hdr_t *hdr = rt_heap_hdr(fabricated->data);
    assert(hdr);

#if SIZE_MAX > INT64_MAX
    hdr->len = (size_t)INT64_MAX + 17;
    int64_t reported = rt_len(fabricated);
    assert(reported == INT64_MAX);
#else
    hdr->len = 5;
    int64_t reported = rt_len(fabricated);
    assert(reported == 5);
#endif

    rt_string_unref(fabricated);
    return 0;
}
