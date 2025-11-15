// File: tests/runtime/RTInstrTests.cpp
// Purpose: Validate runtime INSTR search functions.
// Key invariants: 1-based indexing semantics; empty needle returns clamped
// start; extreme start values clamp to 1.
// Ownership: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
#include "rt.hpp"
#include <cassert>
#include <limits.h>

int main()
{
    rt_string s1 = rt_const_cstr("ABCD");
    rt_string s2 = rt_const_cstr("BC");
    assert(rt_instr2(s1, s2) == 2);

    rt_string s3 = rt_const_cstr("ABABAB");
    rt_string s4 = rt_const_cstr("AB");
    assert(rt_instr3(3, s3, s4) == 3);
    assert(rt_instr3(1, s3, s4) == 1);
    assert(rt_instr3(LLONG_MIN, s3, s4) == rt_instr3(1, s3, s4));

    rt_string empty = rt_const_cstr("");
    assert(rt_instr3(3, s3, empty) == 3);
    assert(rt_instr3(10, s3, empty) == 7);
    assert(rt_instr3(-2, s3, empty) == 1);

    rt_string s5 = rt_const_cstr("ABC");
    rt_string s6 = rt_const_cstr("X");
    assert(rt_instr2(s5, s6) == 0);

    return 0;
}
