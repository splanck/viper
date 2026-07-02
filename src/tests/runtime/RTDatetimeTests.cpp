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
#include "rt_option.h"
#include "rt_string.h"
#include "rt_timezone.h"

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

static int str_eq(rt_string s, const char *expected) {
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
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

static void test_format(void) {
    printf("rt_datetime_format:\n");

    rt_string year = rt_datetime_format(kRef, rt_const_cstr("%Y"));
    check("format extracts year", strcmp(rt_string_cstr(year), "2025") == 0);
    rt_string_unref(year);

    rt_string null_format = rt_datetime_format(kRef, NULL);
    check("null format returns empty", rt_str_len(null_format) == 0);
    rt_string_unref(null_format);

    const char hidden_format[] = "%Y\0-%m";
    rt_string hidden = rt_string_from_bytes(hidden_format, sizeof(hidden_format) - 1);
    rt_string hidden_result = rt_datetime_format(kRef, hidden);
    check("embedded NUL format returns empty", rt_str_len(hidden_result) == 0);
    rt_string_unref(hidden_result);
    rt_string_unref(hidden);
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
    void *epoch_option = rt_datetime_try_parse_option(rt_const_cstr("1970-01-01T00:00:00Z"));
    check("TryParseOption preserves epoch as Some(0)",
          rt_option_is_some(epoch_option) == 1 && rt_option_unwrap_i64(epoch_option) == 0);
    void *invalid_option = rt_datetime_try_parse_option(rt_const_cstr("not-a-date"));
    check("TryParseOption rejects invalid input as None", rt_option_is_none(invalid_option) == 1);
    parsed = rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00.999Z"));
    roundtrip = rt_datetime_to_iso(parsed);
    check("ParseISO accepts fractional UTC seconds",
          strcmp(rt_string_cstr(roundtrip), "2024-01-15T10:30:00Z") == 0);
    rt_string_unref(roundtrip);

    parsed = rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00+02:30"));
    roundtrip = rt_datetime_to_iso(parsed);
    check("ParseISO applies positive timezone offsets",
          strcmp(rt_string_cstr(roundtrip), "2024-01-15T08:00:00Z") == 0);
    rt_string_unref(roundtrip);

    parsed = rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00-05:00"));
    roundtrip = rt_datetime_to_iso(parsed);
    check("ParseISO applies negative timezone offsets",
          strcmp(rt_string_cstr(roundtrip), "2024-01-15T15:30:00Z") == 0);
    rt_string_unref(roundtrip);

    check("ParseISO rejects trailing characters",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00Zx")) == 0);
    check("ParseISO rejects empty fractional seconds",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00.Z")) == 0);
    check("ParseISO rejects malformed timezone offsets",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00+0230")) == 0);
    parsed = rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00+14:00"));
    roundtrip = rt_datetime_to_iso(parsed);
    check("ParseISO accepts maximum timezone offset",
          strcmp(rt_string_cstr(roundtrip), "2024-01-14T20:30:00Z") == 0);
    rt_string_unref(roundtrip);
    check("ParseISO rejects out-of-range timezone hour",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00+24:00")) == 0);
    check("ParseISO rejects timezone offset beyond +14:00",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00+14:01")) == 0);
    check("ParseISO rejects timezone offset beyond -14:00",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00-14:01")) == 0);
    check("ParseISO rejects out-of-range timezone minute",
          rt_datetime_parse_iso(rt_const_cstr("2024-01-15T10:30:00+02:60")) == 0);
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
    check("ParseTime accepts fractional seconds",
          rt_datetime_parse_time(rt_const_cstr("10:30:45.123")) == 37845);
    check("TryParse accepts fractional time",
          rt_datetime_try_parse(rt_const_cstr("10:30:45.123")) == 37845);
    check("ParseTime rejects empty fractional seconds",
          rt_datetime_parse_time(rt_const_cstr("10:30:45.")) == -1);
    check("ParseTime rejects out-of-range hour",
          rt_datetime_parse_time(rt_const_cstr("24:00:00")) == -1);
    check("ParseTime rejects too-short strings", rt_datetime_parse_time(rt_const_cstr("1")) == -1);

    const char hidden_iso[] = "2024-01-15T10:30:00Z\0junk";
    rt_string hidden_iso_s = rt_string_from_bytes(hidden_iso, sizeof(hidden_iso) - 1);
    check("ParseISO rejects embedded NUL suffix", rt_datetime_parse_iso(hidden_iso_s) == 0);
    check("TryParse rejects embedded NUL suffix", rt_datetime_try_parse(hidden_iso_s) == 0);
    rt_string_unref(hidden_iso_s);

    const char hidden_date[] = "2024-01-15\0junk";
    rt_string hidden_date_s = rt_string_from_bytes(hidden_date, sizeof(hidden_date) - 1);
    check("ParseDate rejects embedded NUL suffix", rt_datetime_parse_date(hidden_date_s) == 0);
    rt_string_unref(hidden_date_s);

    const char hidden_time[] = "10:30:00\0junk";
    rt_string hidden_time_s = rt_string_from_bytes(hidden_time, sizeof(hidden_time) - 1);
    check("ParseTime rejects embedded NUL suffix", rt_datetime_parse_time(hidden_time_s) == -1);
    rt_string_unref(hidden_time_s);
}

static void test_create_bounds(void) {
    printf("rt_datetime create bounds:\n");
    check("Create rejects huge year", rt_datetime_create(INT64_MAX, 1, 1, 0, 0, 0) == -1);
    check("Create rejects huge month", rt_datetime_create(2024, INT64_MAX, 1, 0, 0, 0) == -1);
    check("Create rejects huge day", rt_datetime_create(2024, 1, INT64_MAX, 0, 0, 0) == -1);
    check("Create rejects huge hour", rt_datetime_create(2024, 1, 1, INT64_MAX, 0, 0) == -1);
    check("Create rejects month overflow", rt_datetime_create(2024, 13, 1, 0, 0, 0) == -1);
    check("Create rejects day overflow", rt_datetime_create(2024, 1, 32, 0, 0, 0) == -1);
    check("Create rejects invalid leap day", rt_datetime_create(2023, 2, 29, 0, 0, 0) == -1);
    check("Create rejects hour overflow", rt_datetime_create(2024, 1, 1, 24, 0, 0) == -1);
    check("Create rejects minute overflow", rt_datetime_create(2024, 1, 1, 0, 60, 0) == -1);
    check("Create rejects second overflow", rt_datetime_create(2024, 1, 1, 0, 0, 60) == -1);
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

static void test_timezones(void) {
    printf("rt_timezone deterministic named zones:\n");

    void *utc = rt_tz_find(rt_const_cstr("UTC"));
    rt_string utc_name = rt_tz_name(utc);
    check("UTC name", str_eq(utc_name, "UTC"));
    rt_string_unref(utc_name);
    check("UTC offset is zero", rt_tz_offset_at(utc, kRef) == 0);
    check("UTC is not DST", rt_tz_is_dst_at(utc, kRef) == 0);

    void *tokyo = rt_tz_find(rt_const_cstr("Asia/Tokyo"));
    check("Tokyo offset is +09:00", rt_tz_offset_at(tokyo, kRef) == 32400);
    check("Tokyo is not DST", rt_tz_is_dst_at(tokyo, kRef) == 0);
    rt_string tokyo_wall = rt_datetime_to_zone(kRef, tokyo);
    check("Tokyo wall time", str_eq(tokyo_wall, "2025-01-15T19:30:45+09:00"));
    rt_string_unref(tokyo_wall);

    void *ny = rt_tz_find(rt_const_cstr("America/New_York"));
    const int64_t ny_before_spring = 1741503599LL; // 2025-03-09T06:59:59Z
    const int64_t ny_after_spring = 1741503600LL;  // 2025-03-09T07:00:00Z
    check("NY before spring-forward offset", rt_tz_offset_at(ny, ny_before_spring) == -18000);
    check("NY after spring-forward offset", rt_tz_offset_at(ny, ny_after_spring) == -14400);
    check("NY after spring-forward is DST", rt_tz_is_dst_at(ny, ny_after_spring) == 1);
    rt_string ny_before = rt_datetime_to_zone(ny_before_spring, ny);
    rt_string ny_after = rt_datetime_to_zone(ny_after_spring, ny);
    check("NY skipped hour boundary before",
          str_eq(ny_before, "2025-03-09T01:59:59-05:00"));
    check("NY skipped hour boundary after", str_eq(ny_after, "2025-03-09T03:00:00-04:00"));
    rt_string_unref(ny_before);
    rt_string_unref(ny_after);

    rt_string ny_fmt =
        rt_datetime_format_in_zone(ny_after_spring, ny, rt_const_cstr("%F %T %z %Z"));
    check("NY FormatInZone includes offset and abbreviation",
          str_eq(ny_fmt, "2025-03-09 03:00:00 -0400 EDT"));
    rt_string_unref(ny_fmt);

    void *sydney = rt_tz_find(rt_const_cstr("Australia/Sydney"));
    const int64_t syd_before_spring = 1759593599LL; // 2025-10-04T15:59:59Z
    const int64_t syd_after_spring = 1759593600LL;  // 2025-10-04T16:00:00Z
    check("Sydney before DST-start offset",
          rt_tz_offset_at(sydney, syd_before_spring) == 36000);
    check("Sydney after DST-start offset",
          rt_tz_offset_at(sydney, syd_after_spring) == 39600);
    check("Sydney after DST-start is DST", rt_tz_is_dst_at(sydney, syd_after_spring) == 1);
    rt_string syd_before = rt_datetime_to_zone(syd_before_spring, sydney);
    rt_string syd_after = rt_datetime_to_zone(syd_after_spring, sydney);
    check("Sydney skipped hour boundary before",
          str_eq(syd_before, "2025-10-05T01:59:59+10:00"));
    check("Sydney skipped hour boundary after",
          str_eq(syd_after, "2025-10-05T03:00:00+11:00"));
    rt_string_unref(syd_before);
    rt_string_unref(syd_after);

    rt_string bad_format = rt_datetime_format_in_zone(kRef, tokyo, rt_const_cstr("%Q"));
    check("unknown FormatInZone specifier returns empty", rt_str_len(bad_format) == 0);
    rt_string_unref(bad_format);

    EXPECT_TRAP(rt_tz_find(rt_const_cstr("Europe/Paris")));
    check("unknown zone traps", 1);
}

int main(void) {
    printf("=== RTDatetimeTests ===\n");
    test_components();
    test_to_iso();
    test_format();
    test_now();
    test_parsing();
    test_create_bounds();
    test_checked_arithmetic();
    test_timezones();
    printf("All datetime tests passed.\n");
    return 0;
}
