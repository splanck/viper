//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_daterange.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static bool str_eq(rt_string s, const char *expected)
{
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

// Use fixed timestamps: 2025-01-01 00:00:00 = 1735689600
//                        2025-01-31 23:59:59 = 1738367999
//                        2025-02-01 00:00:00 = 1738368000
//                        2025-02-28 23:59:59 = 1740787199

static const int64_t JAN_1 = 1735689600;
static const int64_t JAN_15 = 1735689600 + 14 * 86400;
static const int64_t JAN_31 = 1735689600 + 30 * 86400;
static const int64_t FEB_1 = 1735689600 + 31 * 86400;
static const int64_t FEB_28 = 1735689600 + 58 * 86400;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

static void test_new()
{
    void *r = rt_daterange_new(JAN_1, JAN_31);
    assert(r != NULL);
    assert(rt_daterange_start(r) == JAN_1);
    assert(rt_daterange_end(r) == JAN_31);
}

static void test_new_swapped()
{
    // Should swap start and end if start > end
    void *r = rt_daterange_new(JAN_31, JAN_1);
    assert(rt_daterange_start(r) == JAN_1);
    assert(rt_daterange_end(r) == JAN_31);
}

// ---------------------------------------------------------------------------
// Contains
// ---------------------------------------------------------------------------

static void test_contains()
{
    void *r = rt_daterange_new(JAN_1, JAN_31);
    assert(rt_daterange_contains(r, JAN_15) == 1);
    assert(rt_daterange_contains(r, JAN_1) == 1);  // inclusive start
    assert(rt_daterange_contains(r, JAN_31) == 1);  // inclusive end
    assert(rt_daterange_contains(r, FEB_1) == 0);
    assert(rt_daterange_contains(r, JAN_1 - 1) == 0);
}

// ---------------------------------------------------------------------------
// Overlaps
// ---------------------------------------------------------------------------

static void test_overlaps()
{
    void *jan = rt_daterange_new(JAN_1, JAN_31);
    void *feb = rt_daterange_new(FEB_1, FEB_28);
    void *mid = rt_daterange_new(JAN_15, FEB_1);

    assert(rt_daterange_overlaps(jan, mid) == 1);
    assert(rt_daterange_overlaps(mid, jan) == 1);
    assert(rt_daterange_overlaps(jan, feb) == 0);
}

// ---------------------------------------------------------------------------
// Intersection
// ---------------------------------------------------------------------------

static void test_intersection()
{
    void *jan = rt_daterange_new(JAN_1, JAN_31);
    void *mid = rt_daterange_new(JAN_15, FEB_28);
    void *result = rt_daterange_intersection(jan, mid);

    assert(result != NULL);
    assert(rt_daterange_start(result) == JAN_15);
    assert(rt_daterange_end(result) == JAN_31);
}

static void test_intersection_no_overlap()
{
    void *jan = rt_daterange_new(JAN_1, JAN_15);
    void *feb = rt_daterange_new(FEB_1, FEB_28);
    void *result = rt_daterange_intersection(jan, feb);
    assert(result == NULL);
}

// ---------------------------------------------------------------------------
// Union
// ---------------------------------------------------------------------------

static void test_union()
{
    void *a = rt_daterange_new(JAN_1, JAN_15);
    void *b = rt_daterange_new(JAN_15, JAN_31);
    void *result = rt_daterange_union_range(a, b);

    assert(result != NULL);
    assert(rt_daterange_start(result) == JAN_1);
    assert(rt_daterange_end(result) == JAN_31);
}

static void test_union_gap()
{
    void *a = rt_daterange_new(JAN_1, JAN_15);
    void *b = rt_daterange_new(FEB_1, FEB_28);
    void *result = rt_daterange_union_range(a, b);
    assert(result == NULL);
}

// ---------------------------------------------------------------------------
// Duration queries
// ---------------------------------------------------------------------------

static void test_days()
{
    void *r = rt_daterange_new(JAN_1, JAN_31);
    assert(rt_daterange_days(r) == 30); // 30 days
}

static void test_hours()
{
    void *r = rt_daterange_new(JAN_1, JAN_1 + 7200); // 2 hours
    assert(rt_daterange_hours(r) == 2);
}

static void test_duration()
{
    void *r = rt_daterange_new(JAN_1, JAN_1 + 3600);
    assert(rt_daterange_duration(r) == 3600);
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

static void test_to_string()
{
    void *r = rt_daterange_new(JAN_1, JAN_31);
    rt_string s = rt_daterange_to_string(r);
    const char *cstr = rt_string_cstr(s);
    assert(cstr != NULL);
    // Should contain date format
    assert(strstr(cstr, "2025-01-01") != NULL);
    rt_string_unref(s);
}

// ---------------------------------------------------------------------------
// Null safety
// ---------------------------------------------------------------------------

static void test_null_safety()
{
    assert(rt_daterange_start(NULL) == 0);
    assert(rt_daterange_end(NULL) == 0);
    assert(rt_daterange_contains(NULL, JAN_1) == 0);
    assert(rt_daterange_overlaps(NULL, NULL) == 0);
    assert(rt_daterange_intersection(NULL, NULL) == NULL);
    assert(rt_daterange_union_range(NULL, NULL) == NULL);
    assert(rt_daterange_days(NULL) == 0);
    assert(rt_daterange_hours(NULL) == 0);
    assert(rt_daterange_duration(NULL) == 0);
}

int main()
{
    test_new();
    test_new_swapped();
    test_contains();
    test_overlaps();
    test_intersection();
    test_intersection_no_overlap();
    test_union();
    test_union_gap();
    test_days();
    test_hours();
    test_duration();
    test_to_string();
    test_null_safety();

    return 0;
}
