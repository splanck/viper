// File: tests/unit/runtime/RTFileReadLineOverflowTests.cpp
// Purpose: Ensure rt_file_read_line's buffer growth guard reports overflow instead of reallocating.
// Key invariants: Buffer is released on failure and error surfaces Err_RuntimeError with ERANGE.
// Ownership: Test owns the temporary buffer allocated for exercising the guard.
// Links: docs/codemap.md

#include "rt.hpp"
#include "rt_error.h"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>

extern "C" bool rt_file_line_buffer_try_grow_for_test(char **buffer, size_t *cap, size_t len, RtError *out_err);

int main()
{
    size_t cap = (std::numeric_limits<size_t>::max() / 2) + 1;
    size_t len = cap - 1;
    char *buffer = static_cast<char *>(std::malloc(1));
    assert(buffer != nullptr);

    RtError err = RT_ERROR_NONE;
    bool ok = rt_file_line_buffer_try_grow_for_test(&buffer, &cap, len, &err);

    assert(!ok);
    assert(buffer == nullptr);
    assert(err.kind == Err_RuntimeError);
    assert(err.code == ERANGE);

    return 0;
}
