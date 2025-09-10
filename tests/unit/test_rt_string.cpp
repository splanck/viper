#include "rt.hpp"
#include <cassert>

int main()
{
    rt_string empty = rt_const_cstr("");
    assert(rt_len(empty) == 0);

    rt_string hello = rt_const_cstr("hello");
    rt_string world = rt_const_cstr("world");
    rt_string hw = rt_concat(hello, world);
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
    rt_string bcde = rt_const_cstr("BCDE");
    assert(rt_str_eq(mid_full, bcde));

    rt_string mid_part = rt_mid3(abcde, 1, 2);
    rt_string bc = rt_const_cstr("BC");
    assert(rt_str_eq(mid_part, bc));

    return 0;
}
