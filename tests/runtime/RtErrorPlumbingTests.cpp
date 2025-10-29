// File: tests/runtime/RtErrorPlumbingTests.cpp
// Purpose: Exercise the runtime error plumbing for numeric formatting helpers.
// Key invariants: Formatting helpers populate Err_None on success.
// Ownership: Links against the C runtime library.
// Links: docs/specs/numerics.md

#include "rt_error.h"
#include "rt_numeric.h"

#include <cassert>
#include <cstring>

int main()
{
    char buffer[32];
    RtError err = {Err_RuntimeError, -1};
    rt_str_from_double(42.0, buffer, sizeof(buffer), &err);
    assert(rt_ok(err));
    assert(std::strcmp(buffer, "42") == 0);

    err.kind = Err_RuntimeError;
    err.code = -1;
    rt_str_from_i32(1234, buffer, sizeof(buffer), &err);
    assert(err.kind == Err_None);
    assert(err.code == 0);

    return 0;
}
