//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTDurationTests.cpp
// Purpose: Validate Duration/TimeSpan functions.
//
//===----------------------------------------------------------------------===//

#include "rt_duration.h"
#include "rt_string.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Creation Tests
//=============================================================================

static void test_duration_creation()
{
    printf("Testing Duration creation:\n");

    // Test 1: FromMillis
    {
        int64_t d = rt_duration_from_millis(5000);
        test_result("FromMillis(5000) = 5000", d == 5000);
    }

    // Test 2: FromSeconds
    {
        int64_t d = rt_duration_from_seconds(10);
        test_result("FromSeconds(10) = 10000", d == 10000);
    }

    // Test 3: FromMinutes
    {
        int64_t d = rt_duration_from_minutes(2);
        test_result("FromMinutes(2) = 120000", d == 120000);
    }

    // Test 4: FromHours
    {
        int64_t d = rt_duration_from_hours(1);
        test_result("FromHours(1) = 3600000", d == 3600000);
    }

    // Test 5: FromDays
    {
        int64_t d = rt_duration_from_days(1);
        test_result("FromDays(1) = 86400000", d == 86400000);
    }

    // Test 6: Create from components
    {
        int64_t d = rt_duration_create(1, 2, 30, 45, 500);
        // 1 day + 2 hours + 30 minutes + 45 seconds + 500 ms
        int64_t expected = 86400000 + 7200000 + 1800000 + 45000 + 500;
        test_result("Create(1,2,30,45,500)", d == expected);
    }

    // Test 7: Zero
    {
        int64_t d = rt_duration_zero();
        test_result("Zero() = 0", d == 0);
    }

    printf("\n");
}

//=============================================================================
// Total Conversion Tests
//=============================================================================

static void test_duration_totals()
{
    printf("Testing Duration total conversions:\n");

    int64_t d = rt_duration_create(1, 2, 30, 45, 500);

    // Test 1: TotalMillis
    {
        test_result("TotalMillis", rt_duration_total_millis(d) == d);
    }

    // Test 2: TotalSeconds
    {
        int64_t expected = d / 1000;
        test_result("TotalSeconds", rt_duration_total_seconds(d) == expected);
    }

    // Test 3: TotalMinutes
    {
        int64_t expected = d / 60000;
        test_result("TotalMinutes", rt_duration_total_minutes(d) == expected);
    }

    // Test 4: TotalHours
    {
        int64_t expected = d / 3600000;
        test_result("TotalHours", rt_duration_total_hours(d) == expected);
    }

    // Test 5: TotalDays
    {
        test_result("TotalDays", rt_duration_total_days(d) == 1);
    }

    // Test 6: TotalSecondsF
    {
        double expected = (double)d / 1000.0;
        double actual = rt_duration_total_seconds_f(d);
        test_result("TotalSecondsF", fabs(actual - expected) < 0.001);
    }

    printf("\n");
}

//=============================================================================
// Component Tests
//=============================================================================

static void test_duration_components()
{
    printf("Testing Duration components:\n");

    // 1 day, 2 hours, 30 minutes, 45 seconds, 500 ms
    int64_t d = rt_duration_create(1, 2, 30, 45, 500);

    test_result("Days component", rt_duration_get_days(d) == 1);
    test_result("Hours component", rt_duration_get_hours(d) == 2);
    test_result("Minutes component", rt_duration_get_minutes(d) == 30);
    test_result("Seconds component", rt_duration_get_seconds(d) == 45);
    test_result("Millis component", rt_duration_get_millis(d) == 500);

    // Test with negative duration
    int64_t neg = -d;
    test_result("Negative - Days component", rt_duration_get_days(neg) == 1);
    test_result("Negative - Hours component", rt_duration_get_hours(neg) == 2);

    printf("\n");
}

//=============================================================================
// Operation Tests
//=============================================================================

static void test_duration_operations()
{
    printf("Testing Duration operations:\n");

    int64_t d1 = rt_duration_from_seconds(100);
    int64_t d2 = rt_duration_from_seconds(30);

    test_result("Add", rt_duration_add(d1, d2) == rt_duration_from_seconds(130));
    test_result("Sub", rt_duration_sub(d1, d2) == rt_duration_from_seconds(70));
    test_result("Mul", rt_duration_mul(d2, 3) == rt_duration_from_seconds(90));
    test_result("Div", rt_duration_div(d1, 2) == rt_duration_from_seconds(50));
    test_result("Abs positive", rt_duration_abs(d1) == d1);
    test_result("Abs negative", rt_duration_abs(-d1) == d1);
    test_result("Neg", rt_duration_neg(d1) == -d1);

    // Comparison
    test_result("Cmp d1 > d2", rt_duration_cmp(d1, d2) == 1);
    test_result("Cmp d2 < d1", rt_duration_cmp(d2, d1) == -1);
    test_result("Cmp d1 == d1", rt_duration_cmp(d1, d1) == 0);

    printf("\n");
}

//=============================================================================
// Formatting Tests
//=============================================================================

static void test_duration_formatting()
{
    printf("Testing Duration formatting:\n");

    // Test 1: Simple duration
    {
        int64_t d = rt_duration_create(0, 2, 30, 45, 0);
        rt_string s = rt_duration_to_string(d);
        test_result("ToString 02:30:45", strcmp(rt_string_cstr(s), "02:30:45") == 0);
    }

    // Test 2: With days
    {
        int64_t d = rt_duration_create(1, 2, 30, 45, 0);
        rt_string s = rt_duration_to_string(d);
        test_result("ToString 1.02:30:45", strcmp(rt_string_cstr(s), "1.02:30:45") == 0);
    }

    // Test 3: With millis
    {
        int64_t d = rt_duration_create(0, 0, 1, 30, 500);
        rt_string s = rt_duration_to_string(d);
        test_result("ToString 00:01:30.500", strcmp(rt_string_cstr(s), "00:01:30.500") == 0);
    }

    // Test 4: ISO format
    {
        int64_t d = rt_duration_create(1, 2, 30, 0, 0);
        rt_string s = rt_duration_to_iso(d);
        test_result("ToISO P1DT2H30M", strcmp(rt_string_cstr(s), "P1DT2H30M") == 0);
    }

    // Test 5: ISO format with seconds
    {
        int64_t d = rt_duration_from_seconds(90);
        rt_string s = rt_duration_to_iso(d);
        test_result("ToISO PT1M30S", strcmp(rt_string_cstr(s), "PT1M30S") == 0);
    }

    // Test 6: ISO format zero
    {
        int64_t d = rt_duration_zero();
        rt_string s = rt_duration_to_iso(d);
        test_result("ToISO PT0S", strcmp(rt_string_cstr(s), "PT0S") == 0);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Duration Tests ===\n\n");

    test_duration_creation();
    test_duration_totals();
    test_duration_components();
    test_duration_operations();
    test_duration_formatting();

    printf("All Duration tests passed!\n");
    return 0;
}
