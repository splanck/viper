//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_reltime.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>
#include <ctime>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static bool str_eq(rt_string s, const char *expected)
{
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

// ---------------------------------------------------------------------------
// format_from tests (deterministic, no dependency on current time)
// ---------------------------------------------------------------------------

static void test_just_now()
{
    int64_t now = 1000000;
    rt_string r = rt_reltime_format_from(now + 3, now);
    assert(str_eq(r, "just now"));
    rt_string_unref(r);

    r = rt_reltime_format_from(now - 5, now);
    assert(str_eq(r, "just now"));
    rt_string_unref(r);
}

static void test_seconds_ago()
{
    int64_t now = 1000000;
    rt_string r = rt_reltime_format_from(now - 30, now);
    assert(str_eq(r, "30 seconds ago"));
    rt_string_unref(r);
}

static void test_one_second_ago()
{
    int64_t now = 1000000;
    // 10 seconds is the threshold for "just now"
    // test exactly at boundary
    rt_string r = rt_reltime_format_from(now - 10, now);
    assert(str_eq(r, "10 seconds ago"));
    rt_string_unref(r);
}

static void test_minutes_ago()
{
    int64_t now = 1000000;
    rt_string r = rt_reltime_format_from(now - 120, now);
    assert(str_eq(r, "2 minutes ago"));
    rt_string_unref(r);
}

static void test_one_minute_ago()
{
    int64_t now = 1000000;
    rt_string r = rt_reltime_format_from(now - 60, now);
    assert(str_eq(r, "1 minute ago"));
    rt_string_unref(r);
}

static void test_hours_ago()
{
    int64_t now = 1000000;
    rt_string r = rt_reltime_format_from(now - 7200, now);
    assert(str_eq(r, "2 hours ago"));
    rt_string_unref(r);
}

static void test_days_ago()
{
    int64_t now = 1000000;
    rt_string r = rt_reltime_format_from(now - 86400 * 5, now);
    assert(str_eq(r, "5 days ago"));
    rt_string_unref(r);
}

static void test_months_ago()
{
    int64_t now = 1000000;
    rt_string r = rt_reltime_format_from(now - 86400 * 60, now);
    assert(str_eq(r, "2 months ago"));
    rt_string_unref(r);
}

static void test_years_ago()
{
    int64_t now = 1000000000;
    rt_string r = rt_reltime_format_from(now - 86400 * 400, now);
    assert(str_eq(r, "1 year ago"));
    rt_string_unref(r);
}

static void test_in_future()
{
    int64_t now = 1000000;
    rt_string r = rt_reltime_format_from(now + 3600, now);
    assert(str_eq(r, "in 1 hour"));
    rt_string_unref(r);

    r = rt_reltime_format_from(now + 86400 * 3, now);
    assert(str_eq(r, "in 3 days"));
    rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// format_duration tests
// ---------------------------------------------------------------------------

static void test_duration_seconds()
{
    rt_string r = rt_reltime_format_duration(5000);
    assert(str_eq(r, "5s"));
    rt_string_unref(r);
}

static void test_duration_minutes()
{
    rt_string r = rt_reltime_format_duration(150000); // 2.5 minutes
    assert(str_eq(r, "2m 30s"));
    rt_string_unref(r);
}

static void test_duration_hours_minutes()
{
    rt_string r = rt_reltime_format_duration(9000000); // 2h 30m
    assert(str_eq(r, "2h 30m"));
    rt_string_unref(r);
}

static void test_duration_days()
{
    rt_string r = rt_reltime_format_duration(104400000LL); // 1d 5h
    assert(str_eq(r, "1d 5h"));
    rt_string_unref(r);
}

static void test_duration_zero()
{
    rt_string r = rt_reltime_format_duration(0);
    assert(str_eq(r, "0s"));
    rt_string_unref(r);
}

static void test_duration_negative()
{
    rt_string r = rt_reltime_format_duration(-5000);
    assert(str_eq(r, "-5s"));
    rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// format_short tests
// ---------------------------------------------------------------------------

static void test_short_format()
{
    // We test format_from instead to avoid time dependency
    // format_short uses current time internally, so we just verify it returns a valid string
    int64_t now = (int64_t)time(NULL);

    rt_string r = rt_reltime_format_short(now);
    const char *cstr = rt_string_cstr(r);
    assert(cstr != NULL);
    assert(str_eq(r, "now"));
    rt_string_unref(r);
}

int main()
{
    // format_from (relative to reference)
    test_just_now();
    test_seconds_ago();
    test_one_second_ago();
    test_minutes_ago();
    test_one_minute_ago();
    test_hours_ago();
    test_days_ago();
    test_months_ago();
    test_years_ago();
    test_in_future();

    // format_duration
    test_duration_seconds();
    test_duration_minutes();
    test_duration_hours_minutes();
    test_duration_days();
    test_duration_zero();
    test_duration_negative();

    // format_short
    test_short_format();

    return 0;
}
