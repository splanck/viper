// File: tests/runtime/RTInstrTests.cpp
// Purpose: Validate runtime INSTR search functions.
// Key invariants: 1-based indexing semantics; empty needle returns start.
// Ownership: Uses runtime library.
// Links: docs/runtime-abi.md
#include "rt.hpp"
#include <cassert>

int main()
{
    rt_string s1 = rt_const_cstr("ABCD");
    rt_string s2 = rt_const_cstr("BC");
    assert(rt_instr2(s1, s2) == 2);

    rt_string s3 = rt_const_cstr("ABABAB");
    rt_string s4 = rt_const_cstr("AB");
    assert(rt_instr3(3, s3, s4) == 3);

    rt_string s5 = rt_const_cstr("ABC");
    rt_string s6 = rt_const_cstr("X");
    assert(rt_instr2(s5, s6) == 0);

    rt_string s7 = rt_const_cstr("abc");
    rt_string s8 = rt_const_cstr("a");
    assert(rt_instr3(1, s7, s8) == 1);

    return 0;
}
