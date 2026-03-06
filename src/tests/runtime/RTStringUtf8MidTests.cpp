//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStringUtf8MidTests.cpp
// Purpose: Verify that rt_str_mid and rt_str_mid_len use codepoint-based
//          offsets, correctly handling multi-byte UTF-8 characters.
// Key invariants:
//   - Mid("Hello 世界", 7) returns "世界" (starts at 7th codepoint).
//   - MidLen("café", 4, 1) returns "é" (not a partial byte).
//   - ASCII strings behave identically to byte-based indexing.
// Links: src/runtime/core/rt_string_ops.c
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

//=============================================================================
// Mid (2-arg, 1-based codepoint offset)
//=============================================================================

static void test_mid_ascii()
{
    printf("Testing Mid (ASCII):\n");

    rt_string s = make_str("hello world");
    rt_string r = rt_str_mid(s, 7);
    test_result("Mid(\"hello world\", 7) == \"world\"",
                strcmp(rt_string_cstr(r), "world") == 0);

    rt_string r2 = rt_str_mid(s, 1);
    test_result("Mid(\"hello world\", 1) == \"hello world\"",
                strcmp(rt_string_cstr(r2), "hello world") == 0);
    printf("\n");
}

static void test_mid_utf8()
{
    printf("Testing Mid (UTF-8):\n");

    // "Hello 世界" — 8 codepoints: H(1) e(2) l(3) l(4) o(5) (6) 世(7) 界(8)
    // In bytes: 6 ASCII + 3+3 = 12 bytes total
    rt_string s = make_str("Hello \xe4\xb8\x96\xe7\x95\x8c");

    rt_string r1 = rt_str_mid(s, 7);
    test_result("Mid(\"Hello 世界\", 7) == \"世界\"",
                strcmp(rt_string_cstr(r1), "\xe4\xb8\x96\xe7\x95\x8c") == 0);

    rt_string r2 = rt_str_mid(s, 8);
    test_result("Mid(\"Hello 世界\", 8) == \"界\"",
                strcmp(rt_string_cstr(r2), "\xe7\x95\x8c") == 0);

    // "café" — 4 codepoints: c(1) a(2) f(3) é(4)  (é = 0xC3 0xA9 = 2 bytes)
    rt_string cafe = make_str("caf\xc3\xa9");
    rt_string r3 = rt_str_mid(cafe, 4);
    test_result("Mid(\"café\", 4) == \"é\"",
                strcmp(rt_string_cstr(r3), "\xc3\xa9") == 0);

    printf("\n");
}

//=============================================================================
// MidLen (3-arg, 1-based codepoint offset + codepoint count)
//=============================================================================

static void test_midlen_utf8()
{
    printf("Testing MidLen (UTF-8):\n");

    // "Hello 世界!" — 9 codepoints
    rt_string s = make_str("Hello \xe4\xb8\x96\xe7\x95\x8c!");

    rt_string r1 = rt_str_mid_len(s, 7, 2);
    test_result("MidLen(\"Hello 世界!\", 7, 2) == \"世界\"",
                strcmp(rt_string_cstr(r1), "\xe4\xb8\x96\xe7\x95\x8c") == 0);

    rt_string r2 = rt_str_mid_len(s, 7, 1);
    test_result("MidLen(\"Hello 世界!\", 7, 1) == \"世\"",
                strcmp(rt_string_cstr(r2), "\xe4\xb8\x96") == 0);

    // "café" — MidLen(s, 3, 2) should be "fé"
    rt_string cafe = make_str("caf\xc3\xa9");
    rt_string r3 = rt_str_mid_len(cafe, 3, 2);
    test_result("MidLen(\"café\", 3, 2) == \"fé\"",
                strcmp(rt_string_cstr(r3), "f\xc3\xa9") == 0);

    // Emoji: "Hi 🌍!" — 5 codepoints: H(1) i(2) (3) 🌍(4) !(5)
    // 🌍 = F0 9F 8C 8D (4 bytes)
    rt_string emoji = make_str("Hi \xf0\x9f\x8c\x8d!");
    rt_string r4 = rt_str_mid_len(emoji, 4, 1);
    test_result("MidLen(\"Hi 🌍!\", 4, 1) == \"🌍\"",
                strcmp(rt_string_cstr(r4), "\xf0\x9f\x8c\x8d") == 0);

    printf("\n");
}

static void test_midlen_boundary()
{
    printf("Testing MidLen boundary cases:\n");

    rt_string s = make_str("abc");
    rt_string r1 = rt_str_mid_len(s, 1, 100);
    test_result("MidLen past end clamps", strcmp(rt_string_cstr(r1), "abc") == 0);

    rt_string r2 = rt_str_mid(s, 10);
    test_result("Mid past end returns empty", rt_str_len(r2) == 0);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT String UTF-8 Mid Tests ===\n\n");

    test_mid_ascii();
    test_mid_utf8();
    test_midlen_utf8();
    test_midlen_boundary();

    printf("All String UTF-8 Mid tests passed!\n");
    return 0;
}
