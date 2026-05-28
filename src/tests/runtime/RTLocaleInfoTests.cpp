//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTLocaleInfoTests.cpp
// Purpose: Validate Viper.Localization.LocaleInfo queries against the baked
//          en-US record. Each field is round-tripped through both a parsed
//          Locale handle and an unregistered Locale (which should fall back
//          to the invariant defaults matching en-US).
//
//===----------------------------------------------------------------------===//

#include "rt_locale.h"
#include "rt_locale_info.h"
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

static bool str_eq(rt_string s, const char *expected) {
    const char *cs = rt_string_cstr(s);
    bool ok = cs && strcmp(cs, expected) == 0;
    rt_string_unref(s);
    return ok;
}

//=============================================================================
// en-US field queries
//=============================================================================

static void test_en_us_display_name() {
    printf("Testing LocaleInfo on en-US:\n");

    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);

    test_result("DisplayName(en-US) = \"English (United States)\"",
                str_eq(rt_locale_info_display_name(loc, nullptr), "English (United States)"));
    test_result("LanguageName(en-US) = \"English\"",
                str_eq(rt_locale_info_language_name(loc, nullptr), "English"));
    test_result("RegionName(en-US) = \"United States\"",
                str_eq(rt_locale_info_region_name(loc, nullptr), "United States"));
}

static void test_en_us_text_direction() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);

    test_result("TextDirection(en-US) = \"ltr\"",
                str_eq(rt_locale_info_text_direction(loc), "ltr"));
    test_result("IsRightToLeft(en-US) = false", rt_locale_info_is_rtl(loc) == 0);
}

static void test_en_us_calendar() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);

    test_result("FirstDayOfWeek(en-US) = 0 (Sunday)", rt_locale_info_first_day_of_week(loc) == 0);
}

static void test_en_us_measurement_and_currency() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);

    test_result("MeasurementSystem(en-US) = \"us\"", str_eq(rt_locale_info_measurement(loc), "us"));
    test_result("Currency(en-US) = \"USD\"", str_eq(rt_locale_info_currency(loc), "USD"));
}

//=============================================================================
// Null / unregistered fallback
//=============================================================================

static void test_null_locale_falls_back() {
    printf("Testing LocaleInfo null-fallback:\n");

    // NULL locale — should not trap, should yield the invariant's values
    // which for Phase 1 are the baked en-US record.
    test_result("DisplayName(NULL) falls back to en-US",
                str_eq(rt_locale_info_display_name(nullptr, nullptr), "English (United States)"));
    test_result("TextDirection(NULL) = \"ltr\"",
                str_eq(rt_locale_info_text_direction(nullptr), "ltr"));
    test_result("FirstDayOfWeek(NULL) = 0", rt_locale_info_first_day_of_week(nullptr) == 0);
}

static void test_unregistered_locale_falls_back() {
    printf("Testing LocaleInfo on unregistered locale:\n");

    // A parsed-but-unregistered locale falls through to invariant (en-US)
    // for any field query. Useful for programs that parse user input
    // before loading locale data.
    rt_string in = S("fr-FR");
    void *loc = rt_locale_try_parse(in);
    rt_string_unref(in);

    test_result("DisplayName(fr-FR unregistered) = en-US fallback",
                str_eq(rt_locale_info_display_name(loc, nullptr), "English (United States)"));
    test_result("Currency(fr-FR unregistered) = USD fallback",
                str_eq(rt_locale_info_currency(loc), "USD"));
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT LocaleInfo Tests ===\n\n");
    test_en_us_display_name();
    test_en_us_text_direction();
    test_en_us_calendar();
    test_en_us_measurement_and_currency();
    test_null_locale_falls_back();
    test_unregistered_locale_falls_back();
    printf("\nAll LocaleInfo tests passed!\n");
    return 0;
}
