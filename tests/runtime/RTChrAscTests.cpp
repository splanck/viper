// File: tests/runtime/RTChrAscTests.cpp
// Purpose: Validate CHR$ and ASC runtime helpers.
// Key invariants: CHR$ validates 0-255 range; ASC returns 0 for empty string.
// Ownership: Uses runtime library.
// Links: docs/runtime-abi.md
#include "rt.hpp"
#include <cassert>

int main()
{
    rt_string c = rt_chr(65);
    assert(rt_str_eq(c, rt_const_cstr("A")));

    assert(rt_asc(rt_const_cstr("A")) == 65);
    assert(rt_asc(rt_const_cstr("")) == 0);

    return 0;
}
