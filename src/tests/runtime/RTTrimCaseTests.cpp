//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTrimCaseTests.cpp
// Purpose: Validate string trimming and case mapping runtime helpers. 
// Key invariants: Whitespace includes only space and tab; case maps affect ASCII letters only.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include <cassert>

int main()
{
    rt_string lt = rt_ltrim(rt_const_cstr(" hi"));
    assert(rt_str_eq(lt, rt_const_cstr("hi")));

    rt_string rt = rt_rtrim(rt_const_cstr("hi "));
    assert(rt_str_eq(rt, rt_const_cstr("hi")));

    rt_string t = rt_trim(rt_const_cstr(" hi "));
    assert(rt_str_eq(t, rt_const_cstr("hi")));

    rt_string u = rt_ucase(rt_const_cstr("Abc!"));
    assert(rt_str_eq(u, rt_const_cstr("ABC!")));

    rt_string l = rt_lcase(rt_const_cstr("AbC!"));
    assert(rt_str_eq(l, rt_const_cstr("abc!")));

    rt_string empty = rt_const_cstr("");
    rt_string et = rt_trim(empty);
    assert(rt_str_eq(et, empty));

    return 0;
}
