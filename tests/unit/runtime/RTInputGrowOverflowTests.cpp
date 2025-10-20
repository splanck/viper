// File: tests/unit/runtime/RTInputGrowOverflowTests.cpp
// Purpose: Validate rt_input_try_grow guards against size_t overflow before reallocating.
// Key invariants: Buffer pointer remains unchanged and helper reports overflow instead of reallocating.
// Ownership: Allocates a small runtime buffer and releases it after the check.
// Links: docs/codemap.md

#include "rt.hpp"
#include "rt_internal.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <limits>

int main()
{
    size_t cap = (std::numeric_limits<size_t>::max() / 2) + 1;
    char *buf = (char *)rt_alloc(1);
    assert(buf != nullptr);

    char *before = buf;
    rt_input_grow_result result = rt_input_try_grow(&buf, &cap);

    assert(result == RT_INPUT_GROW_OVERFLOW);
    assert(buf == before);
    assert(cap == (std::numeric_limits<size_t>::max() / 2) + 1);

    free(buf);
    return 0;
}

