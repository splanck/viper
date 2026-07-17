//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTDateFormatTests.cpp
// Purpose: Validate Zanna.Localization.DateFormat against the baked en-US
//          locale using a fixed timestamp for reproducibility. Every pattern
//          letter (y M d E H h m s a) is exercised across its supported
//          repetition counts, plus the four canonical styles and the
//          MonthName / DayName / AmPm queries.
//
//===----------------------------------------------------------------------===//

#include "rt_dateformat.h"
#include "rt_datetime.h"
#include "rt_locale.h"
#include "rt_locale_manager.h"
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
    snprintf(buf, sizeof(buf), "%s/zanna_datefmt_%ld_%s", base, (long)TEST_GETPID(), name);
    TEST_MKDIR(buf);
    return std::string(buf);
}

static void write_text_file(const std::string &path, const char *text) {
    FILE *f = fopen(path.c_str(), "wb");
    assert(f && "failed to create temp date locale JSON");
    size_t len = strlen(text);
    assert(fwrite(text, 1, len, f) == len);
    fclose(f);
}

static const char *DATE_CUSTOM_JSON = R"json({
  "tag": "ar-XD",
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
  "dates": {
    "patterns": {
      "datetime_short": "h:mm a 'on' M/d/yy",
      "datetime_medium": "h:mm:ss a 'on' MMM d, yyyy"
    }
  }
})json";

static void load_date_locale() {
    std::string dir = temp_dir("ar_XD");
    std::string file = dir + "/ar-XD.json";
    write_text_file(file, DATE_CUSTOM_JSON);
    rt_string path = S(file.c_str());
    rt_locale_manager_load_from_json(path);
    rt_string_unref(path);
}

static void *en_fmt() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return rt_dateformat_for_locale(loc);
}

static void *fmt_for_tag(const char *tag) {
    rt_string in = S(tag);
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return rt_dateformat_for_locale(loc);
}

// Fixed reference timestamp: use Zanna's rt_datetime_create with
// UTC-aware components. The locale's baked data is in English, so we use
// month = March (numeric 3) and day = 15, hour 14:30:05 to exercise AM/PM.
static int64_t ref_ts() {
    // 2027-03-15 14:30:05 UTC
    return rt_datetime_create(2027, 3, 15, 14, 30, 5);
}

// A morning reference for AM / h vs H testing.
static int64_t morning_ts() {
    // 2027-03-15 09:07:00 UTC
    return rt_datetime_create(2027, 3, 15, 9, 7, 0);
}

//=============================================================================
// Canonical styles (en-US)
//=============================================================================

static void test_canonical_styles() {
    printf("Testing DateFormat canonical styles (en-US):\n");
    void *fmt = en_fmt();
    int64_t ts = ref_ts();

    test_result("Short = 3/15/27", eq(rt_dateformat_short(fmt, ts), "3/15/27"));
    test_result("Medium = Mar 15, 2027", eq(rt_dateformat_medium(fmt, ts), "Mar 15, 2027"));
    test_result("Long = March 15, 2027", eq(rt_dateformat_long(fmt, ts), "March 15, 2027"));

    // Full includes the weekday name; 2027-03-15 is a Monday.
    rt_string full_str = rt_dateformat_full(fmt, ts);
    const char *full_cs = rt_string_cstr(full_str);
    test_result("Full contains Monday", full_cs && strstr(full_cs, "Monday") != nullptr);
    test_result("Full contains March", full_cs && strstr(full_cs, "March") != nullptr);
    rt_string_unref(full_str);
}

//=============================================================================
// Time styles
//=============================================================================

static void test_time_styles() {
    printf("Testing DateFormat time styles:\n");
    void *fmt = en_fmt();
    int64_t ts = ref_ts();

    test_result("TimeShort = 2:30 PM", eq(rt_dateformat_time_short(fmt, ts), "2:30 PM"));
    test_result("TimeMedium = 2:30:05 PM", eq(rt_dateformat_time_medium(fmt, ts), "2:30:05 PM"));

    // DateTime composites.
    rt_string dts = rt_dateformat_datetime_short(fmt, ts);
    const char *dts_cs = rt_string_cstr(dts);
    test_result("DateTimeShort starts with date", dts_cs && strstr(dts_cs, "3/15/27") != nullptr);
    test_result("DateTimeShort contains time", dts_cs && strstr(dts_cs, "2:30 PM") != nullptr);
    rt_string_unref(dts);
}

static void test_loaded_datetime_and_digits() {
    printf("Testing loaded DateFormat datetime/digit data:\n");
    load_date_locale();
    void *fmt = fmt_for_tag("ar-XD");
    int64_t ts = ref_ts();

    const char *expected_dt = "\xD9\xA2:\xD9\xA3\xD9\xA0 PM on "
                              "\xD9\xA3/\xD9\xA1\xD9\xA5/\xD9\xA2\xD9\xA7";
    test_result("DateTimeShort uses locale composition pattern and digits",
                eq(rt_dateformat_datetime_short(fmt, ts), expected_dt));

    rt_string pattern = S("yyyy-MM-dd HH:mm");
    const char *expected_custom = "\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA7-"
                                  "\xD9\xA0\xD9\xA3-"
                                  "\xD9\xA1\xD9\xA5 "
                                  "\xD9\xA1\xD9\xA4:\xD9\xA3\xD9\xA0";
    test_result("Custom pattern uses locale digits",
                eq(rt_dateformat_custom(fmt, ts, pattern), expected_custom));
    rt_string_unref(pattern);
}

//=============================================================================
// Custom patterns — per-letter coverage
//=============================================================================

static void test_year_patterns() {
    printf("Testing Custom year patterns:\n");
    void *fmt = en_fmt();
    int64_t ts = ref_ts();

    rt_string pat1 = S("y");
    test_result("y -> 2027", eq(rt_dateformat_custom(fmt, ts, pat1), "2027"));
    rt_string_unref(pat1);

    rt_string pat2 = S("yy");
    test_result("yy -> 27", eq(rt_dateformat_custom(fmt, ts, pat2), "27"));
    rt_string_unref(pat2);

    rt_string pat4 = S("yyyy");
    test_result("yyyy -> 2027", eq(rt_dateformat_custom(fmt, ts, pat4), "2027"));
    rt_string_unref(pat4);

    std::string long_year(200, 'y');
    rt_string pat_long = S(long_year.c_str());
    rt_string rendered = rt_dateformat_custom(fmt, ts, pat_long);
    test_result("200 y-pattern run does not truncate/OOB", rt_str_len(rendered) == 200);
    rt_string_unref(rendered);
    rt_string_unref(pat_long);
}

static void test_month_patterns() {
    printf("Testing Custom month patterns:\n");
    void *fmt = en_fmt();
    int64_t ts = ref_ts();

    rt_string pat1 = S("M");
    test_result("M -> 3", eq(rt_dateformat_custom(fmt, ts, pat1), "3"));
    rt_string_unref(pat1);

    rt_string pat2 = S("MM");
    test_result("MM -> 03", eq(rt_dateformat_custom(fmt, ts, pat2), "03"));
    rt_string_unref(pat2);

    rt_string pat3 = S("MMM");
    test_result("MMM -> Mar", eq(rt_dateformat_custom(fmt, ts, pat3), "Mar"));
    rt_string_unref(pat3);

    rt_string pat4 = S("MMMM");
    test_result("MMMM -> March", eq(rt_dateformat_custom(fmt, ts, pat4), "March"));
    rt_string_unref(pat4);
}

static void test_day_and_weekday_patterns() {
    printf("Testing Custom day/weekday patterns:\n");
    void *fmt = en_fmt();
    int64_t ts = ref_ts();

    rt_string d1 = S("d");
    test_result("d -> 15", eq(rt_dateformat_custom(fmt, ts, d1), "15"));
    rt_string_unref(d1);

    rt_string d2 = S("dd");
    test_result("dd -> 15", eq(rt_dateformat_custom(fmt, ts, d2), "15"));
    rt_string_unref(d2);

    rt_string e3 = S("E");
    test_result("E -> Mon", eq(rt_dateformat_custom(fmt, ts, e3), "Mon"));
    rt_string_unref(e3);

    rt_string e4 = S("EEEE");
    test_result("EEEE -> Monday", eq(rt_dateformat_custom(fmt, ts, e4), "Monday"));
    rt_string_unref(e4);
}

static void test_time_patterns() {
    printf("Testing Custom time patterns:\n");
    void *fmt = en_fmt();
    int64_t ts_pm = ref_ts();
    int64_t ts_am = morning_ts();

    rt_string h1 = S("h");
    test_result("h (14h)  -> 2", eq(rt_dateformat_custom(fmt, ts_pm, h1), "2"));
    rt_string_unref(h1);

    rt_string hh = S("hh");
    test_result("hh (09h) -> 09", eq(rt_dateformat_custom(fmt, ts_am, hh), "09"));
    rt_string_unref(hh);

    rt_string H1 = S("H");
    test_result("H (14h)  -> 14", eq(rt_dateformat_custom(fmt, ts_pm, H1), "14"));
    rt_string_unref(H1);

    rt_string HH = S("HH");
    test_result("HH (09h) -> 09", eq(rt_dateformat_custom(fmt, ts_am, HH), "09"));
    rt_string_unref(HH);

    rt_string m1 = S("m");
    test_result("m -> 30", eq(rt_dateformat_custom(fmt, ts_pm, m1), "30"));
    rt_string_unref(m1);

    rt_string mm = S("mm");
    test_result("mm (07min) -> 07", eq(rt_dateformat_custom(fmt, ts_am, mm), "07"));
    rt_string_unref(mm);

    rt_string s1 = S("s");
    test_result("s -> 5", eq(rt_dateformat_custom(fmt, ts_pm, s1), "5"));
    rt_string_unref(s1);

    rt_string ss = S("ss");
    test_result("ss (05s) -> 05", eq(rt_dateformat_custom(fmt, ts_pm, ss), "05"));
    rt_string_unref(ss);

    rt_string a_pm = S("a");
    test_result("a (14h) -> PM", eq(rt_dateformat_custom(fmt, ts_pm, a_pm), "PM"));
    rt_string_unref(a_pm);

    rt_string a_am = S("a");
    test_result("a (09h) -> AM", eq(rt_dateformat_custom(fmt, ts_am, a_am), "AM"));
    rt_string_unref(a_am);
}

static void test_quoted_literals() {
    printf("Testing Custom quoted literals:\n");
    void *fmt = en_fmt();
    int64_t ts = ref_ts();

    rt_string pat1 = S("yyyy'T'HH:mm");
    test_result("yyyy'T'HH:mm -> 2027T14:30",
                eq(rt_dateformat_custom(fmt, ts, pat1), "2027T14:30"));
    rt_string_unref(pat1);

    rt_string pat2 = S("'it''s' yyyy");
    test_result("'it''s' yyyy -> it's 2027", eq(rt_dateformat_custom(fmt, ts, pat2), "it's 2027"));
    rt_string_unref(pat2);

    rt_string pat3 = S("yyyy-MM-dd");
    test_result("yyyy-MM-dd -> 2027-03-15", eq(rt_dateformat_custom(fmt, ts, pat3), "2027-03-15"));
    rt_string_unref(pat3);
}

//=============================================================================
// Name queries
//=============================================================================

static void test_month_name_queries() {
    printf("Testing MonthName queries:\n");
    void *fmt = en_fmt();

    test_result("MonthName(1,wide) = January", eq(rt_dateformat_month_name(fmt, 1, 0), "January"));
    test_result("MonthName(1,abbr) = Jan", eq(rt_dateformat_month_name(fmt, 1, 1), "Jan"));
    test_result("MonthName(12,wide) = December",
                eq(rt_dateformat_month_name(fmt, 12, 0), "December"));
    test_result("MonthName(6,abbr) = Jun", eq(rt_dateformat_month_name(fmt, 6, 1), "Jun"));

    EXPECT_TRAP(rt_dateformat_month_name(fmt, 0, 0));
    test_result("MonthName(0) traps", true);
    EXPECT_TRAP(rt_dateformat_month_name(fmt, 13, 0));
    test_result("MonthName(13) traps", true);
}

static void test_day_name_queries() {
    printf("Testing DayName queries:\n");
    void *fmt = en_fmt();

    test_result("DayName(0,wide) = Sunday", eq(rt_dateformat_day_name(fmt, 0, 0), "Sunday"));
    test_result("DayName(1,abbr) = Mon", eq(rt_dateformat_day_name(fmt, 1, 1), "Mon"));
    test_result("DayName(6,wide) = Saturday", eq(rt_dateformat_day_name(fmt, 6, 0), "Saturday"));

    EXPECT_TRAP(rt_dateformat_day_name(fmt, 7, 0));
    test_result("DayName(7) traps", true);
}

static void test_ampm_queries() {
    printf("Testing AmPm queries:\n");
    void *fmt = en_fmt();
    test_result("AmPm(false) = AM", eq(rt_dateformat_am_pm(fmt, 0), "AM"));
    test_result("AmPm(true)  = PM", eq(rt_dateformat_am_pm(fmt, 1), "PM"));
}

//=============================================================================
// Trap paths
//=============================================================================

static void test_trap_paths() {
    printf("Testing DateFormat trap paths:\n");
    void *fmt = en_fmt();
    int64_t ts = ref_ts();

    rt_string bad_letter = S("X");
    EXPECT_TRAP(rt_dateformat_custom(fmt, ts, bad_letter));
    rt_string_unref(bad_letter);
    test_result("Custom with unknown letter traps", true);

    rt_string bad_quote = S("yyyy-'unterminated");
    EXPECT_TRAP(rt_dateformat_custom(fmt, ts, bad_quote));
    rt_string_unref(bad_quote);
    test_result("Custom with unterminated quote traps", true);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT DateFormat Tests ===\n\n");
    test_canonical_styles();
    test_time_styles();
    test_loaded_datetime_and_digits();
    test_year_patterns();
    test_month_patterns();
    test_day_and_weekday_patterns();
    test_time_patterns();
    test_quoted_literals();
    test_month_name_queries();
    test_day_name_queries();
    test_ampm_queries();
    test_trap_paths();
    printf("\nAll DateFormat tests passed!\n");
    return 0;
}
