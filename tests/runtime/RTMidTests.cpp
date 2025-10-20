// File: tests/runtime/RTMidTests.cpp
// Purpose: Validate MID$ runtime functions honor 1-based semantics.
// Key invariants: Start arguments clamp to the first character and out-of-range
//                 indices yield empty strings without trapping.
// Ownership: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi

#include "rt.hpp"
#include <cassert>

int main()
{
    rt_string source = rt_const_cstr("ABCDE");

    rt_string mid_full = rt_mid2(source, 1);
    assert(mid_full == source);

    rt_string mid_tail = rt_mid2(source, 2);
    rt_string bcde = rt_const_cstr("BCDE");
    assert(rt_str_eq(mid_tail, bcde));

    rt_string mid_oob = rt_mid2(source, 10);
    assert(rt_len(mid_oob) == 0);

    rt_string mid_prefix = rt_mid3(source, 1, 2);
    rt_string ab = rt_const_cstr("AB");
    assert(rt_str_eq(mid_prefix, ab));

    rt_string mid_inner = rt_mid3(source, 2, 2);
    rt_string bc = rt_const_cstr("BC");
    assert(rt_str_eq(mid_inner, bc));

    rt_string mid_oob_len = rt_mid3(source, 10, 3);
    assert(rt_len(mid_oob_len) == 0);

    return 0;
}
