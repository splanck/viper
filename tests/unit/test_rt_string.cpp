// File: tests/unit/test_rt_string.cpp
// Purpose: Verify runtime string helpers including substring operations clamp inputs.
// Key invariants: Substring operations clamp start/length and avoid overflow.
// Ownership: Uses runtime library.
// Links: docs/codemap.md
#include "rt.hpp"
#include "rt_internal.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <setjmp.h>

namespace
{
    static jmp_buf g_trap_jmp;
    static const char *g_last_trap = nullptr;
    static bool g_trap_expected = false;
}

extern "C" void vm_trap(const char *msg)
{
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

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
    assert(rt_str_eq(mid_full, abcde));

    rt_string mid_part = rt_mid3(abcde, 1, 2);
    rt_string mid_ab = rt_const_cstr("AB");
    assert(rt_str_eq(mid_part, mid_ab));

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
        auto *left_impl = (rt_string_impl *)left_owned;
        auto *right_impl = (rt_string_impl *)right_owned;
        assert(left_impl->heap == nullptr);
        assert(right_impl->heap == nullptr);
        size_t left_before = left_impl->literal_refs;
        size_t right_before = right_impl->literal_refs;
        rt_string joined = rt_concat(rt_string_ref(left_owned), rt_string_ref(right_owned));
        assert(left_impl->literal_refs == left_before);
        assert(right_impl->literal_refs == right_before);
        rt_string_unref(joined);
        rt_string_unref(left_owned);
        rt_string_unref(right_owned);
    }

    {
        rt_string base = rt_const_cstr("dup");
        auto *base_impl = (rt_string_impl *)base;
        assert(base_impl->heap == nullptr);
        size_t before = base_impl->literal_refs;
        rt_string doubled = rt_concat(rt_string_ref(base), rt_string_ref(base));
        assert(base_impl->literal_refs == before);
        rt_string_unref(doubled);
        rt_string_unref(base);
    }

    {
        rt_string left_heap = rt_string_from_bytes("heap", 4);
        rt_string right_heap = rt_string_from_bytes("data", 4);
        auto *left_impl = (rt_string_impl *)left_heap;
        auto *right_impl = (rt_string_impl *)right_heap;
        assert(left_impl->heap != nullptr);
        assert(right_impl->heap != nullptr);
        size_t left_before = left_impl->heap->refcnt;
        size_t right_before = right_impl->heap->refcnt;
        rt_string merged = rt_concat(rt_string_ref(left_heap), rt_string_ref(right_heap));
        assert(left_impl->heap->refcnt == left_before);
        assert(right_impl->heap->refcnt == right_before);
        assert(std::strcmp(((rt_string_impl *)merged)->data, "heapdata") == 0);
        rt_string_unref(merged);
        rt_string_unref(left_heap);
        rt_string_unref(right_heap);
    }

    {
        rt_string_impl huge_literal = {nullptr, nullptr, std::numeric_limits<size_t>::max(), 0};
        rt_string_impl small_literal = {nullptr, nullptr, 16, 0};
        g_last_trap = nullptr;
        g_trap_expected = true;
        if (setjmp(g_trap_jmp) == 0)
        {
            (void)rt_concat(&huge_literal, &small_literal);
            assert(!"rt_concat should trap on overflow");
        }
        else
        {
            assert(g_last_trap != nullptr);
            assert(std::strcmp(g_last_trap, "rt_concat: length overflow") == 0);
        }
        g_trap_expected = false;
        g_last_trap = nullptr;
    }

    return 0;
}
