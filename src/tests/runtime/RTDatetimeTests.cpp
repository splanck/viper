//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTDatetimeTests.cpp
// Purpose: Validate rt_datetime_* API (Viper.Time.DateTime).
// Key invariants: Year/month/day/hour/minute/second extraction from a known
//                 Unix timestamp must be correct; ISO 8601 output is parseable.
// Ownership/Lifetime: Returned rt_string values are released after each test.
//
// Reference timestamp: 2025-01-15 10:30:45 UTC = 1736937045
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "rt_datetime.h"
#include "rt_string.h"

#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void check(const char *label, int ok) {
    printf("  %-50s %s\n", label, ok ? "PASS" : "FAIL");
    assert(ok);
}

static int str_contains(rt_string s, const char *needle) {
    const char *cstr = rt_string_cstr(s);
    if (!cstr)
        return 0;
    return strstr(cstr, needle) != NULL;
}

// Known reference timestamp: 2025-01-15 10:30:45 UTC
// (Verified with: date -d "2025-01-15 10:30:45 UTC" +%s)
static const int64_t kRef = 1736937045LL;

static void test_components(void) {
    // rt_datetime_* decomposes via localtime (not gmtime).  Only year is
    // guaranteed stable across all UTC offsets for this mid-January timestamp.
    printf("rt_datetime component extraction (ts=%lld):\n", (long long)kRef);
    check("year == 2025", rt_datetime_year(kRef) == 2025);
}

static void test_to_iso(void) {
    printf("rt_datetime_to_iso:\n");
    rt_string iso = rt_datetime_to_iso(kRef);
    check("iso non-empty", rt_str_len(iso) > 0);
    check("iso contains '2025'", str_contains(iso, "2025"));
    check("iso contains 'T'", str_contains(iso, "T"));
    check("iso contains 'Z'", str_contains(iso, "Z"));
    rt_string_unref(iso);

    // Epoch produces "1970-01-01T00:00:00Z"
    rt_string epoch_iso = rt_datetime_to_iso(0);
    check("epoch iso contains '1970'", str_contains(epoch_iso, "1970"));
    rt_string_unref(epoch_iso);
}

static void test_now(void) {
    printf("rt_datetime_now:\n");
    int64_t ts = rt_datetime_now();
    // Must be after 2020-01-01 (ts=1577836800) and before 2100-01-01 (ts=4102444800)
    check("now > 2020", ts > 1577836800LL);
    check("now < 2100", ts < 4102444800LL);

    int64_t ms = rt_datetime_now_ms();
    check("now_ms > 0", ms > 0);
    check("now_ms >= now * 1000", ms >= ts * 1000LL);
}

static void test_parsing(void) {
    printf("rt_datetime parsing:\n");

    int64_t parsed = rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00Z"));
    rt_string roundtrip = rt_datetime_to_iso(parsed);
    check("UTC parse round-trips through ToISO",
          strcmp(rt_string_cstr(roundtrip), "2024-01-15T10:30:00Z") == 0);
    rt_string_unref(roundtrip);

    check("epoch ISO is a successful zero timestamp",
          rt_datetime_parse_iso(rt_const_cstr("1970-01-01T00:00:00Z")) == 0);
    check("TryParse accepts epoch ISO",
          rt_datetime_try_parse(rt_const_cstr("1970-01-01T00:00:00Z")) == 0);
    check("ParseISO rejects trailing characters",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00Zx")) == 0);
    check("ParseISO rejects invalid calendar date",
          rt_datetime_parse_iso(rt_const_cstr("2024-02-30T00:00:00Z")) == 0);
    check("ParseISO rejects invalid time",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T24:00:00Z")) == 0);
    check("ParseISO rejects too-short strings", rt_datetime_parse_iso(rt_const_cstr("1")) == 0);
    check("ParseDate rejects trailing characters",
          rt_datetime_parse_date(rt_const_cstr("2024-01-15x")) == 0);
    check("ParseDate rejects invalid calendar date",
          rt_datetime_parse_date(rt_const_cstr("2024-02-30")) == 0);
    check("ParseDate rejects too-short strings", rt_datetime_parse_date(rt_const_cstr("1")) == 0);
    check("ParseTime rejects trailing characters",
          rt_datetime_parse_time(rt_const_cstr("10:30:00x")) == -1);
    check("ParseTime rejects out-of-range hour",
          rt_datetime_parse_time(rt_const_cstr("24:00:00")) == -1);
    check("ParseTime rejects too-short strings", rt_datetime_parse_time(rt_const_cstr("1")) == -1);
}

static void test_create_bounds(void) {
    printf("rt_datetime create bounds:\n");
    check("Create rejects huge year", rt_datetime_create(INT64_MAX, 1, 1, 0, 0, 0) == -1);
    check("Create rejects huge month", rt_datetime_create(2024, INT64_MAX, 1, 0, 0, 0) == -1);
    check("Create rejects huge day", rt_datetime_create(2024, 1, INT64_MAX, 0, 0, 0) == -1);
    check("Create rejects huge hour", rt_datetime_create(2024, 1, 1, INT64_MAX, 0, 0) == -1);

    int64_t normalized = rt_datetime_create(2024, 13, 1, 0, 0, 0);
    check("Create still normalizes in-range overflow components",
          normalized != -1 && rt_datetime_year(normalized) == 2025 &&
              rt_datetime_month(normalized) == 1);
}

static void test_checked_arithmetic(void) {
    printf("rt_datetime checked arithmetic:\n");
    check("AddSeconds normal case", rt_datetime_add_seconds(100, 25) == 125);
    check("AddDays normal case", rt_datetime_add_days(100, 2) == 172900);
    check("Diff normal case", rt_datetime_diff(125, 100) == 25);
    EXPECT_TRAP(rt_datetime_add_seconds(INT64_MAX, 1));
    EXPECT_TRAP(rt_datetime_add_days(INT64_MAX, 1));
    EXPECT_TRAP(rt_datetime_diff(INT64_MIN, 1));
    check("overflow cases trap", 1);
}

int main(void) {
    printf("=== RTDatetimeTests ===\n");
    test_components();
    test_to_iso();
    test_now();
    test_parsing();
    test_create_bounds();
    test_checked_arithmetic();
    printf("All datetime tests passed.\n");
    return 0;
}
