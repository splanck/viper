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

//=============================================================================
// DateOnly Tests
//=============================================================================

static void test_dateonly_creation()
{
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

static void test_dateonly_parsing()
{
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

    printf("\n");
}

static void test_dateonly_components()
{
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

    printf("\n");
}

static void test_dateonly_arithmetic()
{
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

    printf("\n");
}

static void test_dateonly_queries()
{
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

static void test_dateonly_comparison()
{
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

static void test_dateonly_formatting()
{
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

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT DateOnly Tests ===\n\n");

    test_dateonly_creation();
    test_dateonly_parsing();
    test_dateonly_components();
    test_dateonly_arithmetic();
    test_dateonly_queries();
    test_dateonly_comparison();
    test_dateonly_formatting();

    printf("All DateOnly tests passed!\n");
    return 0;
}
