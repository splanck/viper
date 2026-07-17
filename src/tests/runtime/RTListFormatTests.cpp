//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTListFormatTests.cpp
// Purpose: Validate Zanna.Localization.ListFormat against baked en-US
//          templates. Covers 0/1/2/3/many-item cases across And / Or /
//          Unit styles.
//
//===----------------------------------------------------------------------===//

#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_list.h"
#include "rt_list_format.h"
#include "rt_locale.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static rt_string S(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static bool eq(rt_string s, const char *expected) {
    const char *cs = rt_string_cstr(s);
    bool ok = cs && strcmp(cs, expected) == 0;
    rt_string_unref(s);
    return ok;
}

static void *en_fmt() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return rt_list_format_for_locale(loc);
}

static void *mk_list(const char **items) {
    void *l = rt_list_new();
    while (*items) {
        rt_list_push(l, S(*items));
        ++items;
    }
    return l;
}

//=============================================================================
// And — varying item count
//=============================================================================

static void test_and_sizes() {
    printf("Testing ListFormat.And (en-US):\n");
    void *fmt = en_fmt();

    const char *e0[] = {nullptr};
    test_result("And([]) = \"\"", eq(rt_list_format_and(fmt, mk_list(e0)), ""));

    const char *e1[] = {"apples", nullptr};
    test_result("And([apples]) = \"apples\"", eq(rt_list_format_and(fmt, mk_list(e1)), "apples"));

    const char *e2[] = {"apples", "bananas", nullptr};
    test_result("And([apples, bananas]) = \"apples and bananas\"",
                eq(rt_list_format_and(fmt, mk_list(e2)), "apples and bananas"));

    const char *e3[] = {"apples", "bananas", "cherries", nullptr};
    test_result("And([a,b,c]) = \"apples, bananas, and cherries\"",
                eq(rt_list_format_and(fmt, mk_list(e3)), "apples, bananas, and cherries"));

    const char *e4[] = {"a", "b", "c", "d", nullptr};
    test_result("And([a,b,c,d]) = \"a, b, c, and d\"",
                eq(rt_list_format_and(fmt, mk_list(e4)), "a, b, c, and d"));
}

//=============================================================================
// Or
//=============================================================================

static void test_or_sizes() {
    printf("Testing ListFormat.Or (en-US):\n");
    void *fmt = en_fmt();

    const char *e2[] = {"red", "blue", nullptr};
    test_result("Or([red, blue]) = \"red or blue\"",
                eq(rt_list_format_or(fmt, mk_list(e2)), "red or blue"));

    const char *e3[] = {"red", "green", "blue", nullptr};
    test_result("Or([r,g,b]) = \"red, green, or blue\"",
                eq(rt_list_format_or(fmt, mk_list(e3)), "red, green, or blue"));
}

//=============================================================================
// Unit
//=============================================================================

static void test_unit_sizes() {
    printf("Testing ListFormat.Unit (en-US):\n");
    void *fmt = en_fmt();

    const char *e2[] = {"3h", "45m", nullptr};
    test_result("Unit([3h, 45m]) = \"3h 45m\"",
                eq(rt_list_format_unit(fmt, mk_list(e2)), "3h 45m"));

    const char *e3[] = {"1d", "2h", "3m", nullptr};
    test_result("Unit([1d, 2h, 3m]) = \"1d 2h 3m\"",
                eq(rt_list_format_unit(fmt, mk_list(e3)), "1d 2h 3m"));
}

//=============================================================================
// Short (mirrors And in baked en-US v1)
//=============================================================================

static void test_short() {
    printf("Testing ListFormat.Short (en-US):\n");
    void *fmt = en_fmt();

    const char *e3[] = {"a", "b", "c", nullptr};
    test_result("Short([a,b,c]) mirrors And in v1",
                eq(rt_list_format_short(fmt, mk_list(e3)), "a, b, and c"));
}

//=============================================================================
// Main
//=============================================================================

static void test_no_reference_leaks() {
    printf("Testing ListFormat reference balance (VDOC-075):\n");
    void *fmt = en_fmt();

    // Long unique strings force real heap allocations (short literals may be
    // interned or SSO-backed and have no heap header).
    const char *e3[] = {"alpha-element-with-a-long-heap-backed-payload-0001",
                        "alpha-element-with-a-long-heap-backed-payload-0002",
                        "alpha-element-with-a-long-heap-backed-payload-0003", nullptr};
    void *list = mk_list(e3);
    rt_string first = (rt_string)rt_list_get(list, 0);
    size_t before = rt_heap_hdr(first->data)->refcnt;
    rt_string_unref(first);

    for (int i = 0; i < 8; i++) {
        rt_string out = rt_list_format_and(fmt, list);
        rt_string_unref(out);
    }

    rt_string again = (rt_string)rt_list_get(list, 0);
    size_t after = rt_heap_hdr(again->data)->refcnt;
    rt_string_unref(again);
    test_result("element refcount unchanged after 8 formats", before == after);

    // One-item join returns an owned reference without over-retaining.
    const char *e1[] = {"solo-element-with-a-long-heap-backed-payload-0001", nullptr};
    void *one = mk_list(e1);
    rt_string solo_before = (rt_string)rt_list_get(one, 0);
    size_t base = rt_heap_hdr(solo_before->data)->refcnt;
    rt_string_unref(solo_before);
    rt_string joined = rt_list_format_and(fmt, one);
    rt_string_unref(joined);
    rt_string solo_after = (rt_string)rt_list_get(one, 0);
    test_result("one-item join is reference balanced",
                rt_heap_hdr(solo_after->data)->refcnt == base);
    rt_string_unref(solo_after);
}

int main() {
    printf("=== RT ListFormat Tests ===\n\n");
    test_no_reference_leaks();
    test_and_sizes();
    test_or_sizes();
    test_unit_sizes();
    test_short();
    printf("\nAll ListFormat tests passed!\n");
    return 0;
}
