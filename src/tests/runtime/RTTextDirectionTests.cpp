//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTextDirectionTests.cpp
// Purpose: Validate Viper.Localization.TextDirection detection and wrapping
//          across LTR-only, RTL-only, mixed, and neutral-only inputs. Uses
//          canonical codepoints from each supported RTL script (Hebrew,
//          Arabic) to ensure the classification table matches the plan.
//
//===----------------------------------------------------------------------===//

#include "rt_locale.h"
#include "rt_string.h"
#include "rt_text_direction.h"

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

// UTF-8 literals for strong-RTL codepoints:
//   Hebrew "שלום" = U+05E9 U+05DC U+05D5 U+05DD
//   Arabic "مرحبا" = U+0645 U+0631 U+062D U+0628 U+0627
static const char *HEBREW_SHALOM = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
static const char *ARABIC_HELLO  = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";

//=============================================================================
// Detect
//=============================================================================

static void test_detect_ltr() {
    printf("Testing Detect — LTR inputs:\n");
    rt_string a = S("Hello world");
    test_result("Detect(Hello world) = ltr",
                eq(rt_text_direction_detect(a), "ltr"));
    rt_string_unref(a);

    rt_string b = S("");
    test_result("Detect(empty) = empty",
                eq(rt_text_direction_detect(b), ""));
    rt_string_unref(b);

    rt_string c = S("12345");
    test_result("Detect(all digits) = ltr",
                eq(rt_text_direction_detect(c), "ltr"));
    rt_string_unref(c);
}

static void test_detect_rtl() {
    printf("Testing Detect — RTL inputs:\n");
    rt_string heb = S(HEBREW_SHALOM);
    test_result("Detect(Hebrew) = rtl",
                eq(rt_text_direction_detect(heb), "rtl"));
    rt_string_unref(heb);

    rt_string ar = S(ARABIC_HELLO);
    test_result("Detect(Arabic) = rtl",
                eq(rt_text_direction_detect(ar), "rtl"));
    rt_string_unref(ar);

    // Arabic Extended-A codepoint U+08A0 should also classify RTL.
    rt_string ext = S("\xE0\xA2\xA0");
    test_result("Detect(Arabic Extended-A) = rtl",
                eq(rt_text_direction_detect(ext), "rtl"));
    rt_string_unref(ext);
}

static void test_detect_mixed() {
    printf("Testing Detect — mixed inputs:\n");
    char buf[64];
    snprintf(buf, sizeof(buf), "hello %s world", HEBREW_SHALOM);
    rt_string m = S(buf);
    test_result("Detect(english + hebrew) = mixed",
                eq(rt_text_direction_detect(m), "mixed"));
    rt_string_unref(m);
}

//=============================================================================
// IsRTL / IsLTR
//=============================================================================

static void test_is_rtl_is_ltr() {
    printf("Testing IsRTL / IsLTR:\n");
    rt_string a = S("abc");
    test_result("IsLTR(abc) = true", rt_text_direction_is_ltr(a) == 1);
    test_result("IsRTL(abc) = false", rt_text_direction_is_rtl(a) == 0);
    rt_string_unref(a);

    rt_string heb = S(HEBREW_SHALOM);
    test_result("IsRTL(hebrew) = true", rt_text_direction_is_rtl(heb) == 1);
    test_result("IsLTR(hebrew) = false", rt_text_direction_is_ltr(heb) == 0);
    rt_string_unref(heb);

    rt_string empty = S("");
    test_result("IsLTR(empty) = true (default)",
                rt_text_direction_is_ltr(empty) == 1);
    rt_string_unref(empty);
}

//=============================================================================
// FirstStrong
//=============================================================================

static void test_first_strong() {
    printf("Testing FirstStrong:\n");
    rt_string a = S("  !? abc");
    test_result("FirstStrong(leading neutrals + abc) = ltr",
                eq(rt_text_direction_first_strong(a), "ltr"));
    rt_string_unref(a);

    char buf[64];
    snprintf(buf, sizeof(buf), "12 %s xyz", HEBREW_SHALOM);
    rt_string mixed = S(buf);
    test_result("FirstStrong(\"12 <hebrew> xyz\") = rtl",
                eq(rt_text_direction_first_strong(mixed), "rtl"));
    rt_string_unref(mixed);

    rt_string neutrals = S("1234 !?");
    test_result("FirstStrong(all neutral) = neutral",
                eq(rt_text_direction_first_strong(neutrals), "neutral"));
    rt_string_unref(neutrals);
}

//=============================================================================
// Bidi wrapping
//=============================================================================

static void test_bidi() {
    printf("Testing Bidi wrapping:\n");
    // Pure-LTR pass through unchanged.
    rt_string pure_ltr = S("Hello");
    rt_string wrapped = rt_text_direction_bidi(pure_ltr);
    test_result("Bidi(pure LTR) unchanged",
                eq(wrapped, "Hello"));
    rt_string_unref(pure_ltr);

    // Mixed gets RLI...PDI isolates around the RTL run.
    char buf[64];
    snprintf(buf, sizeof(buf), "a%sb", HEBREW_SHALOM);
    rt_string mixed = S(buf);
    rt_string bidi = rt_text_direction_bidi(mixed);
    const char *bcs = rt_string_cstr(bidi);
    // Expect marks to be present.
    bool has_rli = bcs && strstr(bcs, "\xE2\x81\xA7") != nullptr;
    bool has_pdi = bcs && strstr(bcs, "\xE2\x81\xA9") != nullptr;
    bool no_rlo = !bcs || strstr(bcs, "\xE2\x80\xAE") == nullptr;
    test_result("Bidi(mixed) contains RLI isolate", has_rli);
    test_result("Bidi(mixed) contains PDI isolate", has_pdi);
    test_result("Bidi(mixed) does not use RLO override", no_rlo);
    rt_string_unref(mixed);
    rt_string_unref(bidi);
}

static void test_malformed_utf8() {
    printf("Testing malformed UTF-8 detection stability:\n");
    rt_string bad = S("\xC0\xAF");
    test_result("Detect(malformed UTF-8) defaults ltr",
                eq(rt_text_direction_detect(bad), "ltr"));
    rt_string_unref(bad);
}

//=============================================================================
// OfLocale
//=============================================================================

static void test_of_locale() {
    printf("Testing OfLocale:\n");
    rt_string en_tag = S("en-US");
    void *en = rt_locale_parse(en_tag);
    rt_string_unref(en_tag);
    test_result("OfLocale(en-US) = ltr",
                eq(rt_text_direction_of_locale(en), "ltr"));
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT TextDirection Tests ===\n\n");
    test_detect_ltr();
    test_detect_rtl();
    test_detect_mixed();
    test_is_rtl_is_ltr();
    test_first_strong();
    test_bidi();
    test_malformed_utf8();
    test_of_locale();
    printf("\nAll TextDirection tests passed!\n");
    return 0;
}
