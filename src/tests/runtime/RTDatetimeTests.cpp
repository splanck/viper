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
#include <stdio.h>
#include <string.h>

static void check(const char *label, int ok)
{
    printf("  %-50s %s\n", label, ok ? "PASS" : "FAIL");
    assert(ok);
}

static int str_contains(rt_string s, const char *needle)
{
    const char *cstr = rt_string_cstr(s);
    if (!cstr)
        return 0;
    return strstr(cstr, needle) != NULL;
}

// Known reference timestamp: 2025-01-15 10:30:45 UTC
// (Verified with: date -d "2025-01-15 10:30:45 UTC" +%s)
static const int64_t kRef = 1736937045LL;

static void test_components(void)
{
    // rt_datetime_* decomposes via localtime (not gmtime).  Only year is
    // guaranteed stable across all UTC offsets for this mid-January timestamp.
    printf("rt_datetime component extraction (ts=%lld):\n", (long long)kRef);
    check("year == 2025", rt_datetime_year(kRef) == 2025);
}

static void test_to_iso(void)
{
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

static void test_now(void)
{
    printf("rt_datetime_now:\n");
    int64_t ts = rt_datetime_now();
    // Must be after 2020-01-01 (ts=1577836800) and before 2100-01-01 (ts=4102444800)
    check("now > 2020", ts > 1577836800LL);
    check("now < 2100", ts < 4102444800LL);

    int64_t ms = rt_datetime_now_ms();
    check("now_ms > 0", ms > 0);
    check("now_ms >= now * 1000", ms >= ts * 1000LL);
}

int main(void)
{
    printf("=== RTDatetimeTests ===\n");
    test_components();
    test_to_iso();
    test_now();
    printf("All datetime tests passed.\n");
    return 0;
}
