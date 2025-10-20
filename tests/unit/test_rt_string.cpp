// File: tests/unit/test_rt_string.cpp
// Purpose: Verify runtime string helpers including substring operations clamp inputs.
// Key invariants: Substring operations clamp start/length and avoid overflow.
// Ownership: Uses runtime library.
// Links: docs/codemap.md
#include "rt.hpp"
#include "rt_internal.h"
#include <cassert>
#include <limits>

int main()
{
    rt_string empty = rt_const_cstr("");
    assert(rt_len(empty) == 0);

    rt_string hello = rt_const_cstr("hello");
    rt_string world = rt_const_cstr("world");
    rt_string hw = rt_concat(rt_string_ref(hello), rt_string_ref(world));
    assert(rt_len(hw) == 10);
    rt_string helloworld = rt_const_cstr("helloworld");
    assert(rt_str_eq(hw, helloworld));

    rt_string sub0 = rt_substr(hw, 0, 5);
    assert(rt_str_eq(sub0, hello));
    rt_string sub1 = rt_substr(hw, 5, 5);
    assert(rt_str_eq(sub1, world));
    rt_string subempty = rt_substr(hw, 10, 0);
    assert(rt_len(subempty) == 0);

    rt_string clamp1 = rt_substr(hw, 8, 10);
    rt_string ld = rt_const_cstr("ld");
    assert(rt_str_eq(clamp1, ld));
    rt_string clamp2 = rt_substr(hw, -3, 4);
    rt_string hell = rt_const_cstr("hell");
    assert(rt_str_eq(clamp2, hell));
    rt_string clamp3 = rt_substr(hw, 2, -5);
    assert(rt_len(clamp3) == 0);

    int64_t huge = std::numeric_limits<int64_t>::max();
    rt_string biglen = rt_substr(hw, 2, huge);
    rt_string lloworld = rt_const_cstr("lloworld");
    assert(rt_str_eq(biglen, lloworld));
    rt_string bigstart = rt_substr(hw, huge, huge);
    assert(rt_len(bigstart) == 0);

    assert(!rt_str_eq(hello, world));

    rt_string num = rt_const_cstr("  -42 ");
    assert(rt_to_int(num) == -42);

    rt_string abcde = rt_const_cstr("ABCDE");

    rt_string left = rt_left(abcde, 2);
    rt_string ab = rt_const_cstr("AB");
    assert(rt_str_eq(left, ab));

    rt_string right = rt_right(abcde, 3);
    rt_string cde = rt_const_cstr("CDE");
    assert(rt_str_eq(right, cde));

    rt_string mid_full = rt_mid2(abcde, 1);
    assert(mid_full == abcde);

    rt_string mid_tail = rt_mid2(abcde, 2);
    rt_string bcde = rt_const_cstr("BCDE");
    assert(rt_str_eq(mid_tail, bcde));

    rt_string mid_part = rt_mid3(abcde, 1, 2);
    rt_string ab_mid = rt_const_cstr("AB");
    assert(rt_str_eq(mid_part, ab_mid));

    rt_string mid_part_offset = rt_mid3(abcde, 2, 2);
    rt_string bc = rt_const_cstr("BC");
    assert(rt_str_eq(mid_part_offset, bc));

    rt_string full_left = rt_left(abcde, 5);
    assert(full_left == abcde);
    rt_string full_right = rt_right(abcde, 5);
    assert(full_right == abcde);
    rt_string empty_left = rt_left(abcde, 0);
    rt_string empty_mid = rt_mid3(abcde, 2, 0);
    assert(empty_left == empty_mid);

    {
        rt_string left_owned = rt_const_cstr("left");
        rt_string right_owned = rt_const_cstr("right");
        rt_string left_ref = rt_string_ref(left_owned);
        rt_string right_ref = rt_string_ref(right_owned);
        rt_string joined = rt_concat(left_ref, right_ref);
        auto *left_impl = (rt_string_impl *)left_owned;
        auto *right_impl = (rt_string_impl *)right_owned;
        assert(left_impl->heap == nullptr);
        assert(right_impl->heap == nullptr);
        assert(left_impl->literal_refs == 2);
        assert(right_impl->literal_refs == 2);
        rt_string_unref(joined);
        rt_string_unref(left_ref);
        rt_string_unref(right_ref);
        rt_string_unref(left_owned);
        rt_string_unref(right_owned);
    }

    {
        rt_string base = rt_const_cstr("dup");
        rt_string first = rt_string_ref(base);
        rt_string second = rt_string_ref(base);
        rt_string doubled = rt_concat(first, second);
        auto *base_impl = (rt_string_impl *)base;
        assert(base_impl->heap == nullptr);
        assert(base_impl->literal_refs == 3);
        rt_string_unref(doubled);
        rt_string_unref(first);
        rt_string_unref(second);
        rt_string_unref(base);
    }

    return 0;
}
