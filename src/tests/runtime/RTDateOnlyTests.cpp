//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTDateOnlyTests.cpp
// Purpose: Validate DateOnly type.
//
//===----------------------------------------------------------------------===//

#include "rt_dateonly.h"
#include "rt_daterange.h"
#include "rt_string.h"

#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <string>

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

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// DateOnly Tests
//=============================================================================

static void test_dateonly_creation() {
    printf("Testing DateOnly Creation:\n");

    // Test 1: Create valid date
    {
        void *d = rt_dateonly_create(2024, 6, 15);
        test_result("Create valid date", d != NULL);
        test_result("Year is 2024", rt_dateonly_year(d) == 2024);
        test_result("Month is 6", rt_dateonly_month(d) == 6);
        test_result("Day is 15", rt_dateonly_day(d) == 15);
    }

    // Test 2: Create invalid date (month out of range)
    {
        void *d = rt_dateonly_create(2024, 13, 1);
        test_result("Invalid month returns NULL", d == NULL);
    }

    // Test 3: Create invalid date (day out of range)
    {
        void *d = rt_dateonly_create(2024, 2, 30);
        test_result("Invalid day returns NULL", d == NULL);
    }

    // Test 4: Feb 29 in leap year
    {
        void *d = rt_dateonly_create(2024, 2, 29);
        test_result("Feb 29 in leap year valid", d != NULL);
    }

    // Test 5: Feb 29 in non-leap year
    {
        void *d = rt_dateonly_create(2023, 2, 29);
        test_result("Feb 29 in non-leap year invalid", d == NULL);
    }

    // Test 6: Today
    {
        void *d = rt_dateonly_today();
        test_result("Today returns valid date", d != NULL);
        test_result("Today year > 2000", rt_dateonly_year(d) > 2000);
    }

    printf("\n");
}

static void test_dateonly_parsing() {
    printf("Testing DateOnly Parsing:\n");

    // Test 1: Parse valid ISO date
    {
        void *d = rt_dateonly_parse(rt_const_cstr("2024-06-15"));
        test_result("Parse ISO date", d != NULL);
        test_result("Parsed year", rt_dateonly_year(d) == 2024);
        test_result("Parsed month", rt_dateonly_month(d) == 6);
        test_result("Parsed day", rt_dateonly_day(d) == 15);
    }

    // Test 2: Parse invalid format
    {
        void *d = rt_dateonly_parse(rt_const_cstr("not-a-date"));
        test_result("Invalid format returns NULL", d == NULL);
    }

    // Test 3: Strict parser rejects trailing characters
    {
        void *d = rt_dateonly_parse(rt_const_cstr("2024-06-15x"));
        test_result("Trailing characters return NULL", d == NULL);
    }

    // Test 4: Strict parser rejects non-padded fields
    {
        void *d = rt_dateonly_parse(rt_const_cstr("2024-6-15"));
        test_result("Non-padded date returns NULL", d == NULL);
    }

    // Test 5: Strict parser rejects invalid dates
    {
        void *d = rt_dateonly_parse(rt_const_cstr("2024-02-30"));
        test_result("Invalid parsed date returns NULL", d == NULL);
    }

    // Test 6: Runtime length is authoritative even when a C string prefix looks valid
    {
        const char hidden[] = "2024-06-15\0junk";
        rt_string hidden_s = rt_string_from_bytes(hidden, sizeof(hidden) - 1);
        void *d = rt_dateonly_parse(hidden_s);
        test_result("Embedded NUL suffix returns NULL", d == NULL);
        rt_string_unref(hidden_s);
    }

    printf("\n");
}

static void test_dateonly_components() {
    printf("Testing DateOnly Components:\n");

    void *d = rt_dateonly_create(2024, 7, 4); // July 4, 2024 (Thursday)

    // Test 1: Day of week (Thursday = 4)
    {
        int64_t dow = rt_dateonly_day_of_week(d);
        test_result("Day of week for July 4, 2024", dow == 4);
    }

    // Test 2: Day of year
    {
        int64_t doy = rt_dateonly_day_of_year(d);
        // Jan 31 + Feb 29 + Mar 31 + Apr 30 + May 31 + Jun 30 + Jul 4 = 186
        test_result("Day of year", doy == 186);
    }

    // Test 3: To/from days since epoch
    {
        int64_t days = rt_dateonly_to_days(d);
        void *d2 = rt_dateonly_from_days(days);
        test_result("Round-trip to days", rt_dateonly_equals(d, d2) == 1);
    }

    // Test 4: Dates before the Unix epoch normalize weekday into 0..6
    {
        void *pre_epoch = rt_dateonly_create(1969, 12, 27); // Saturday
        test_result("Pre-epoch day of week is non-negative",
                    rt_dateonly_day_of_week(pre_epoch) == 6);
    }

    printf("\n");
}

// VDOC-231: DateOnly enforces the four-digit ISO 8601 year domain [0,9999] at
// every entry point, so every constructible value round-trips through
// ToString/Parse and out-of-domain inputs are rejected up front instead of
// producing un-serializable dates.
static void test_dateonly_extreme_conversions() {
    printf("Testing DateOnly Year-Domain Enforcement:\n");

    // A day count inside the year domain converts and round-trips by days.
    {
        void *d = rt_dateonly_from_days(0); // 1970-01-01
        test_result("FromDays(0) is the epoch date", d != nullptr);
        test_result("FromDays(0) round-trips by days", rt_dateonly_to_days(d) == 0);
    }

    // A day count whose calendar year exceeds 9999 is rejected rather than
    // producing a date that ToString cannot serialize back.
    {
        void *out_of_domain = rt_dateonly_from_days(3000000); // ~year 10184
        test_result("FromDays past year 9999 returns null", out_of_domain == nullptr);
    }

    // A genuinely overflowing day count still traps in the checked conversion.
    {
        EXPECT_TRAP(rt_dateonly_from_days(INT64_MAX));
        test_result("Overflowing FromDays traps", true);
    }

    // Create enforces the same domain: extreme, five-digit, and negative years
    // are rejected, while the inclusive ISO 8601 bounds 0 and 9999 are accepted.
    {
        test_result("Create rejects INT64_MAX year",
                    rt_dateonly_create(INT64_MAX, 1, 1) == nullptr);
        test_result("Create rejects year 10000", rt_dateonly_create(10000, 1, 1) == nullptr);
        test_result("Create rejects negative year", rt_dateonly_create(-1, 1, 1) == nullptr);
        test_result("Create accepts year 0", rt_dateonly_create(0, 1, 1) != nullptr);
        test_result("Create accepts year 9999", rt_dateonly_create(9999, 12, 31) != nullptr);
    }

    // Round-trip: every constructible value serializes to a fixed ten-byte string
    // that Parse reads back to the same date.
    {
        void *d = rt_dateonly_create(9999, 12, 31);
        rt_string s = rt_dateonly_to_string(d);
        test_result("Max-year ToString is fixed width",
                    strcmp(rt_string_cstr(s), "9999-12-31") == 0);
        void *reparsed = rt_dateonly_parse(s);
        test_result("Max-year string round-trips through Parse", reparsed != nullptr);
        rt_string_unref(s);

        void *d0 = rt_dateonly_create(0, 1, 1);
        rt_string s0 = rt_dateonly_to_string(d0);
        test_result("Year-zero ToString is fixed width",
                    strcmp(rt_string_cstr(s0), "0000-01-01") == 0);
        test_result("Year-zero string round-trips through Parse",
                    rt_dateonly_parse(s0) != nullptr);
        rt_string_unref(s0);
    }

    printf("\n");
}

static void test_dateonly_arithmetic() {
    printf("Testing DateOnly Arithmetic:\n");

    void *d = rt_dateonly_create(2024, 1, 15);

    // Test 1: Add days
    {
        void *d2 = rt_dateonly_add_days(d, 10);
        test_result("Add 10 days", rt_dateonly_day(d2) == 25);
    }

    // Test 2: Add days across month
    {
        void *d2 = rt_dateonly_add_days(d, 20);
        test_result("Add days across month", rt_dateonly_month(d2) == 2);
        test_result("Day after crossing month", rt_dateonly_day(d2) == 4);
    }

    // Test 3: Add months
    {
        void *d2 = rt_dateonly_add_months(d, 3);
        test_result("Add 3 months", rt_dateonly_month(d2) == 4);
    }

    // Test 3b: Add negative months
    {
        void *d2 = rt_dateonly_add_months(d, -1);
        test_result("Subtract 1 month crosses year", rt_dateonly_year(d2) == 2023);
        test_result("Subtract 1 month month", rt_dateonly_month(d2) == 12);
    }

    // Test 4: Add months with day clamping (Jan 31 + 1 month)
    {
        void *jan31 = rt_dateonly_create(2024, 1, 31);
        void *feb = rt_dateonly_add_months(jan31, 1);
        test_result("Day clamped in Feb", rt_dateonly_day(feb) == 29);
    }

    // Test 5: Add years
    {
        void *d2 = rt_dateonly_add_years(d, 2);
        test_result("Add 2 years", rt_dateonly_year(d2) == 2026);
    }

    // Test 6: Add years from Feb 29
    {
        void *leap = rt_dateonly_create(2024, 2, 29);
        void *next_year = rt_dateonly_add_years(leap, 1);
        test_result("Feb 29 + 1 year becomes Feb 28", rt_dateonly_day(next_year) == 28);
    }

    // Test 7: Diff days
    {
        void *d1 = rt_dateonly_create(2024, 1, 1);
        void *d2 = rt_dateonly_create(2024, 1, 11);
        test_result("Diff days", rt_dateonly_diff_days(d2, d1) == 10);
    }

    // Test 8: Large month offsets normalize arithmetically (no iterative loop); a
    // result whose year leaves the [0,9999] domain returns null rather than an
    // un-serializable date (VDOC-231).
    {
        void *d2 = rt_dateonly_add_months(d, INT64_MAX);
        test_result("Huge AddMonths past the year domain returns null", d2 == nullptr);
    }

    // Test 9: Overflowing arithmetic traps
    {
        EXPECT_TRAP(rt_dateonly_add_days(d, INT64_MAX));
        EXPECT_TRAP(rt_dateonly_add_years(d, INT64_MAX));
        test_result("DateOnly overflow traps", true);
    }

    printf("\n");
}

static void test_dateonly_queries() {
    printf("Testing DateOnly Queries:\n");

    // Test 1: Leap year
    {
        void *d2024 = rt_dateonly_create(2024, 1, 1);
        void *d2023 = rt_dateonly_create(2023, 1, 1);
        test_result("2024 is leap year", rt_dateonly_is_leap_year(d2024) == 1);
        test_result("2023 is not leap year", rt_dateonly_is_leap_year(d2023) == 0);
    }

    // Test 2: Days in month
    {
        void *jan = rt_dateonly_create(2024, 1, 1);
        void *feb = rt_dateonly_create(2024, 2, 1);
        void *feb_non_leap = rt_dateonly_create(2023, 2, 1);
        test_result("Jan has 31 days", rt_dateonly_days_in_month(jan) == 31);
        test_result("Feb 2024 has 29 days", rt_dateonly_days_in_month(feb) == 29);
        test_result("Feb 2023 has 28 days", rt_dateonly_days_in_month(feb_non_leap) == 28);
    }

    // Test 3: Start/end of month
    {
        void *d = rt_dateonly_create(2024, 3, 15);
        void *start = rt_dateonly_start_of_month(d);
        void *end = rt_dateonly_end_of_month(d);
        test_result("Start of March is day 1", rt_dateonly_day(start) == 1);
        test_result("End of March is day 31", rt_dateonly_day(end) == 31);
    }

    // Test 4: Start/end of year
    {
        void *d = rt_dateonly_create(2024, 6, 15);
        void *start = rt_dateonly_start_of_year(d);
        void *end = rt_dateonly_end_of_year(d);
        test_result("Start of year is Jan 1",
                    rt_dateonly_month(start) == 1 && rt_dateonly_day(start) == 1);
        test_result("End of year is Dec 31",
                    rt_dateonly_month(end) == 12 && rt_dateonly_day(end) == 31);
    }

    printf("\n");
}

static void test_dateonly_comparison() {
    printf("Testing DateOnly Comparison:\n");

    void *d1 = rt_dateonly_create(2024, 1, 15);
    void *d2 = rt_dateonly_create(2024, 1, 15);
    void *d3 = rt_dateonly_create(2024, 2, 1);

    test_result("Equal dates", rt_dateonly_equals(d1, d2) == 1);
    test_result("Unequal dates", rt_dateonly_equals(d1, d3) == 0);
    test_result("Cmp equal returns 0", rt_dateonly_cmp(d1, d2) == 0);
    test_result("Cmp less returns -1", rt_dateonly_cmp(d1, d3) == -1);
    test_result("Cmp greater returns 1", rt_dateonly_cmp(d3, d1) == 1);

    printf("\n");
}

static void test_dateonly_formatting() {
    printf("Testing DateOnly Formatting:\n");

    void *d = rt_dateonly_create(2024, 7, 4);

    // Test 1: ToString (ISO)
    {
        rt_string s = rt_dateonly_to_string(d);
        test_result("ToString ISO format", strcmp(rt_string_cstr(s), "2024-07-04") == 0);
    }

    // Test 2: Format with year
    {
        rt_string s = rt_dateonly_format(d, rt_const_cstr("%Y"));
        test_result("Format %Y", strcmp(rt_string_cstr(s), "2024") == 0);
    }

    // Test 3: Format with month name
    {
        rt_string s = rt_dateonly_format(d, rt_const_cstr("%B"));
        test_result("Format %B", strcmp(rt_string_cstr(s), "July") == 0);
    }

    // Test 4: Format full date
    {
        rt_string s = rt_dateonly_format(d, rt_const_cstr("%A, %B %d, %Y"));
        test_result("Format full date", strcmp(rt_string_cstr(s), "Thursday, July 04, 2024") == 0);
    }

    // Test 5: Long custom formats should not be truncated
    {
        std::string fmt(300, 'x');
        fmt += "%Y";
        rt_string fmt_s = rt_string_from_bytes(fmt.data(), fmt.size());
        rt_string s = rt_dateonly_format(d, fmt_s);
        std::string expected(300, 'x');
        expected += "2024";
        test_result("Format long custom pattern",
                    rt_str_len(s) == (int64_t)expected.size() &&
                        std::memcmp(rt_string_cstr(s), expected.data(), expected.size()) == 0);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

// VDOC-229: an explicit receiver of another runtime class must trap rather than
// be reinterpreted as a DateOnly payload. A DateRange is a live heap object of a
// different class, so the DateOnly component/arithmetic methods must reject it.
static void test_dateonly_wrong_class_receiver_traps() {
    printf("DateOnly wrong-class receiver traps:\n");

    void *range = rt_daterange_new(0, 86400);
    assert(range != nullptr);

    EXPECT_TRAP(rt_dateonly_year(range));
    EXPECT_TRAP(rt_dateonly_month(range));
    EXPECT_TRAP(rt_dateonly_day(range));
    EXPECT_TRAP(rt_dateonly_day_of_week(range));
    EXPECT_TRAP(rt_dateonly_day_of_year(range));
    EXPECT_TRAP(rt_dateonly_to_days(range));
    EXPECT_TRAP(rt_dateonly_add_days(range, 1));
    EXPECT_TRAP(rt_dateonly_is_leap_year(range));
    EXPECT_TRAP(rt_dateonly_start_of_month(range));
    EXPECT_TRAP(rt_dateonly_to_string(range));

    printf("  PASS\n\n");
}

int main() {
    printf("=== RT DateOnly Tests ===\n\n");

    test_dateonly_creation();
    test_dateonly_parsing();
    test_dateonly_components();
    test_dateonly_extreme_conversions();
    test_dateonly_arithmetic();
    test_dateonly_queries();
    test_dateonly_comparison();
    test_dateonly_formatting();
    test_dateonly_wrong_class_receiver_traps();

    printf("All DateOnly tests passed!\n");
    return 0;
}
