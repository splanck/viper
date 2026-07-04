//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTRelativeTimeFormatTests.cpp
// Purpose: Validate Viper.Localization.RelativeTimeFormat against the baked
//          en-US templates. Covers unit selection across the seven thresholds,
//          past vs. future signage, plural form selection via PluralRules,
//          and the explicit Numeric(value, unit) entry point.
//
//===----------------------------------------------------------------------===//

#include "rt_locale.h"
#include "rt_locale_manager.h"
#include "rt_reltime_format.h"
#include "rt_string.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_GETPID() _getpid()
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_MKDIR(path) mkdir(path, 0700)
#define TEST_GETPID() getpid()
#endif

static jmp_buf g_trap_env;
static int g_expect_trap = 0;

extern "C" void vm_trap(const char *msg) {
    if (g_expect_trap)
        longjmp(g_trap_env, 1);
    fprintf(stderr, "unexpected trap: %s\n", msg ? msg : "(null)");
    abort();
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_expect_trap = 1;                                                                         \
        if (setjmp(g_trap_env) == 0) {                                                             \
            (void)(expr);                                                                          \
            g_expect_trap = 0;                                                                     \
            assert(!"expected runtime trap");                                                      \
        } else {                                                                                   \
            g_expect_trap = 0;                                                                     \
        }                                                                                          \
    } while (0)

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

static std::string temp_dir(const char *name) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/viper_reltime_%ld_%s", base, (long)TEST_GETPID(), name);
    TEST_MKDIR(buf);
    return std::string(buf);
}

static void write_text_file(const std::string &path, const char *text) {
    FILE *f = fopen(path.c_str(), "wb");
    assert(f && "failed to create temp relative-time locale JSON");
    size_t len = strlen(text);
    assert(fwrite(text, 1, len, f) == len);
    fclose(f);
}

static const char *REL_CUSTOM_JSON = R"json({
  "tag": "ar-XR",
  "numbers": {
    "decimal_sep": ".",
    "group_sep": ",",
    "group_size": 3,
    "minus": "-",
    "plus": "+",
    "percent": "%",
    "infinity": "Infinity",
    "nan": "NaN",
    "exponent": "E",
    "digits": "\u0660\u0661\u0662\u0663\u0664\u0665\u0666\u0667\u0668\u0669"
  },
  "relative_time": {
    "now": "right now",
    "past": "{n} {unit} ago",
    "future": "in {n} {unit}",
    "short_past": "{n}{unit} ago",
    "short_future": "in {n}{unit}",
    "short_units": {
      "second": { "one": "s", "other": "s" },
      "minute": { "one": "m", "other": "m" },
      "hour":   { "one": "h", "other": "h" },
      "day":    { "one": "d", "other": "d" },
      "week":   { "one": "w", "other": "w" },
      "month":  { "one": "mo", "other": "mo" },
      "year":   { "one": "y", "other": "y" }
    }
  }
})json";

static void load_rel_locale() {
    std::string dir = temp_dir("ar_XR");
    std::string file = dir + "/ar-XR.json";
    write_text_file(file, REL_CUSTOM_JSON);
    rt_string path = S(file.c_str());
    rt_locale_manager_load_from_json(path);
    rt_string_unref(path);
}

static void *en_rtf() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return rt_reltimefmt_for_locale(loc);
}

static void *rtf_for_tag(const char *tag) {
    rt_string in = S(tag);
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return rt_reltimefmt_for_locale(loc);
}

//=============================================================================
// Unit selection — past
//=============================================================================

static void test_past_units() {
    printf("Testing Format past units (en-US):\n");
    void *f = en_rtf();

    // Sub-second durations yield the neutral relative phrase.
    test_result("Format(500ms) = now", eq(rt_reltimefmt_format(f, 500), "now"));
    test_result("Format(0ms) = now", eq(rt_reltimefmt_format(f, 0), "now"));
    test_result("Format(1000ms) = 1 second ago", eq(rt_reltimefmt_format(f, 1000), "1 second ago"));
    test_result("Format(5s) = 5 seconds ago",
                eq(rt_reltimefmt_format(f, 5 * 1000), "5 seconds ago"));
    test_result("Format(1 min) = 1 minute ago",
                eq(rt_reltimefmt_format(f, 60 * 1000), "1 minute ago"));
    test_result("Format(3 min) = 3 minutes ago",
                eq(rt_reltimefmt_format(f, 3 * 60 * 1000), "3 minutes ago"));
    test_result("Format(1 hr) = 1 hour ago",
                eq(rt_reltimefmt_format(f, 60LL * 60 * 1000), "1 hour ago"));
    test_result("Format(2 hr) = 2 hours ago",
                eq(rt_reltimefmt_format(f, 2LL * 60 * 60 * 1000), "2 hours ago"));
    test_result("Format(1 day) = 1 day ago",
                eq(rt_reltimefmt_format(f, 24LL * 60 * 60 * 1000), "1 day ago"));
    test_result("Format(3 days) = 3 days ago",
                eq(rt_reltimefmt_format(f, 3LL * 24 * 60 * 60 * 1000), "3 days ago"));
    test_result("Format(1 week) = 1 week ago",
                eq(rt_reltimefmt_format(f, 7LL * 24 * 60 * 60 * 1000), "1 week ago"));
    test_result("Format(1 month) = 1 month ago",
                eq(rt_reltimefmt_format(f, 30LL * 24 * 60 * 60 * 1000), "1 month ago"));
    test_result("Format(1 year) = 1 year ago",
                eq(rt_reltimefmt_format(f, 365LL * 24 * 60 * 60 * 1000), "1 year ago"));
}

//=============================================================================
// Unit selection — future (negative durations)
//=============================================================================

static void test_future_units() {
    printf("Testing Format future units:\n");
    void *f = en_rtf();

    test_result("Format(-1s) = in 1 second", eq(rt_reltimefmt_format(f, -1000), "in 1 second"));
    test_result("Format(-5s) = in 5 seconds",
                eq(rt_reltimefmt_format(f, -5 * 1000), "in 5 seconds"));
    test_result("Format(-1 hr) = in 1 hour",
                eq(rt_reltimefmt_format(f, -60LL * 60 * 1000), "in 1 hour"));
    test_result("Format(-3 hr) = in 3 hours",
                eq(rt_reltimefmt_format(f, -3LL * 60 * 60 * 1000), "in 3 hours"));
}

//=============================================================================
// FormatFrom (two timestamps)
//=============================================================================

static void test_format_from() {
    printf("Testing FormatFrom:\n");
    void *f = en_rtf();

    int64_t now = 1700000000; // arbitrary Unix seconds
    int64_t two_hours_ago = now - 2 * 3600;
    int64_t in_one_day = now + 86400;

    test_result("FormatFrom(-2h) = 2 hours ago",
                eq(rt_reltimefmt_format_from(f, two_hours_ago, now), "2 hours ago"));
    test_result("FormatFrom(+1d) = in 1 day",
                eq(rt_reltimefmt_format_from(f, in_one_day, now), "in 1 day"));
}

//=============================================================================
// Numeric entry point
//=============================================================================

static void test_numeric() {
    printf("Testing Numeric(value, unit):\n");
    void *f = en_rtf();

    rt_string day = S("day");
    test_result("Numeric(5, day) past = 5 days ago",
                eq(rt_reltimefmt_numeric(f, 5, day), "5 days ago"));
    rt_string_unref(day);

    rt_string day2 = S("day");
    test_result("Numeric(-2, day) future = in 2 days",
                eq(rt_reltimefmt_numeric(f, -2, day2), "in 2 days"));
    rt_string_unref(day2);

    rt_string hour = S("hour");
    test_result("Numeric(1, hour) = 1 hour ago",
                eq(rt_reltimefmt_numeric(f, 1, hour), "1 hour ago"));
    rt_string_unref(hour);

    rt_string second = S("second");
    test_result("Numeric(0, second) = now", eq(rt_reltimefmt_numeric(f, 0, second), "now"));
    rt_string_unref(second);

    rt_string bad = S("eon");
    EXPECT_TRAP(rt_reltimefmt_numeric(f, 1, bad));
    rt_string_unref(bad);
    test_result("Numeric with unknown unit traps", true);
}

//=============================================================================
// Style property
//=============================================================================

static void test_style() {
    printf("Testing Style property:\n");
    void *f = en_rtf();

    test_result("default Style = long", eq(rt_reltimefmt_get_style(f), "long"));

    rt_string short_style = S("short");
    rt_reltimefmt_set_style(f, short_style);
    rt_string_unref(short_style);
    test_result("Style set to short", eq(rt_reltimefmt_get_style(f), "short"));

    test_result("Short formatter uses short unit table",
                eq(rt_reltimefmt_format(f, 2LL * 60 * 60 * 1000), "2 hr ago"));

    rt_string bad = S("narrow");
    EXPECT_TRAP(rt_reltimefmt_set_style(f, bad));
    rt_string_unref(bad);
    test_result("Unknown style traps", true);
}

static void test_loaded_now_digits_and_short() {
    printf("Testing loaded RelativeTimeFormat data:\n");
    load_rel_locale();
    void *f = rtf_for_tag("ar-XR");

    test_result("Loaded now string is used", eq(rt_reltimefmt_format(f, 0), "right now"));

    rt_string short_style = S("short");
    rt_reltimefmt_set_style(f, short_style);
    rt_string_unref(short_style);
    const char *expected = "\xD9\xA2h ago";
    test_result("Loaded short style localizes digits",
                eq(rt_reltimefmt_format(f, 2LL * 60 * 60 * 1000), expected));
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT RelativeTimeFormat Tests ===\n\n");
    test_past_units();
    test_future_units();
    test_format_from();
    test_numeric();
    test_style();
    test_loaded_now_digits_and_short();
    printf("\nAll RelativeTimeFormat tests passed!\n");
    return 0;
}
