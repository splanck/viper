//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTListFormatTests.cpp
// Purpose: Validate Viper.Localization.ListFormat against baked en-US
//          templates. Covers 0/1/2/3/many-item cases across And / Or /
//          Unit styles.
//
//===----------------------------------------------------------------------===//

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

    const char *e0[] = { nullptr };
    test_result("And([]) = \"\"", eq(rt_list_format_and(fmt, mk_list(e0)), ""));

    const char *e1[] = { "apples", nullptr };
    test_result("And([apples]) = \"apples\"",
                eq(rt_list_format_and(fmt, mk_list(e1)), "apples"));

    const char *e2[] = { "apples", "bananas", nullptr };
    test_result("And([apples, bananas]) = \"apples and bananas\"",
                eq(rt_list_format_and(fmt, mk_list(e2)), "apples and bananas"));

    const char *e3[] = { "apples", "bananas", "cherries", nullptr };
    test_result("And([a,b,c]) = \"apples, bananas, and cherries\"",
                eq(rt_list_format_and(fmt, mk_list(e3)),
                   "apples, bananas, and cherries"));

    const char *e4[] = { "a", "b", "c", "d", nullptr };
    test_result("And([a,b,c,d]) = \"a, b, c, and d\"",
                eq(rt_list_format_and(fmt, mk_list(e4)), "a, b, c, and d"));
}

//=============================================================================
// Or
//=============================================================================

static void test_or_sizes() {
    printf("Testing ListFormat.Or (en-US):\n");
    void *fmt = en_fmt();

    const char *e2[] = { "red", "blue", nullptr };
    test_result("Or([red, blue]) = \"red or blue\"",
                eq(rt_list_format_or(fmt, mk_list(e2)), "red or blue"));

    const char *e3[] = { "red", "green", "blue", nullptr };
    test_result("Or([r,g,b]) = \"red, green, or blue\"",
                eq(rt_list_format_or(fmt, mk_list(e3)),
                   "red, green, or blue"));
}

//=============================================================================
// Unit
//=============================================================================

static void test_unit_sizes() {
    printf("Testing ListFormat.Unit (en-US):\n");
    void *fmt = en_fmt();

    const char *e2[] = { "3h", "45m", nullptr };
    test_result("Unit([3h, 45m]) = \"3h 45m\"",
                eq(rt_list_format_unit(fmt, mk_list(e2)), "3h 45m"));

    const char *e3[] = { "1d", "2h", "3m", nullptr };
    test_result("Unit([1d, 2h, 3m]) = \"1d 2h 3m\"",
                eq(rt_list_format_unit(fmt, mk_list(e3)), "1d 2h 3m"));
}

//=============================================================================
// Short (mirrors And in baked en-US v1)
//=============================================================================

static void test_short() {
    printf("Testing ListFormat.Short (en-US):\n");
    void *fmt = en_fmt();

    const char *e3[] = { "a", "b", "c", nullptr };
    test_result("Short([a,b,c]) mirrors And in v1",
                eq(rt_list_format_short(fmt, mk_list(e3)),
                   "a, b, and c"));
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT ListFormat Tests ===\n\n");
    test_and_sizes();
    test_or_sizes();
    test_unit_sizes();
    test_short();
    printf("\nAll ListFormat tests passed!\n");
    return 0;
}
