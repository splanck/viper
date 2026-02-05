//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_daterange.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string_builder.h"

#include <stdio.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Internal structure
// ---------------------------------------------------------------------------

typedef struct
{
    void *vptr;
    int64_t start; // Unix timestamp in seconds
    int64_t end;   // Unix timestamp in seconds
} rt_daterange_impl;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

void *rt_daterange_new(int64_t start, int64_t end)
{
    // Ensure start <= end
    int64_t s = start <= end ? start : end;
    int64_t e = start <= end ? end : start;

    rt_daterange_impl *r =
        (rt_daterange_impl *)rt_obj_new_i64(0, sizeof(rt_daterange_impl));
    r->start = s;
    r->end = e;
    return r;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int64_t rt_daterange_start(void *range)
{
    if (!range) return 0;
    return ((rt_daterange_impl *)range)->start;
}

int64_t rt_daterange_end(void *range)
{
    if (!range) return 0;
    return ((rt_daterange_impl *)range)->end;
}

// ---------------------------------------------------------------------------
// Containment / overlap
// ---------------------------------------------------------------------------

int64_t rt_daterange_contains(void *range, int64_t timestamp)
{
    if (!range) return 0;
    rt_daterange_impl *r = (rt_daterange_impl *)range;
    return (timestamp >= r->start && timestamp <= r->end) ? 1 : 0;
}

int64_t rt_daterange_overlaps(void *range, void *other)
{
    if (!range || !other) return 0;
    rt_daterange_impl *a = (rt_daterange_impl *)range;
    rt_daterange_impl *b = (rt_daterange_impl *)other;
    return (a->start <= b->end && b->start <= a->end) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Set operations
// ---------------------------------------------------------------------------

void *rt_daterange_intersection(void *range, void *other)
{
    if (!range || !other) return NULL;
    rt_daterange_impl *a = (rt_daterange_impl *)range;
    rt_daterange_impl *b = (rt_daterange_impl *)other;

    int64_t s = a->start > b->start ? a->start : b->start;
    int64_t e = a->end < b->end ? a->end : b->end;

    if (s > e) return NULL; // no overlap
    return rt_daterange_new(s, e);
}

void *rt_daterange_union_range(void *range, void *other)
{
    if (!range || !other) return NULL;
    rt_daterange_impl *a = (rt_daterange_impl *)range;
    rt_daterange_impl *b = (rt_daterange_impl *)other;

    // Check if ranges overlap or are contiguous (within 1 second)
    if (a->start > b->end + 1 || b->start > a->end + 1)
        return NULL; // gap between ranges

    int64_t s = a->start < b->start ? a->start : b->start;
    int64_t e = a->end > b->end ? a->end : b->end;
    return rt_daterange_new(s, e);
}

// ---------------------------------------------------------------------------
// Duration queries
// ---------------------------------------------------------------------------

int64_t rt_daterange_days(void *range)
{
    if (!range) return 0;
    rt_daterange_impl *r = (rt_daterange_impl *)range;
    return (r->end - r->start) / 86400;
}

int64_t rt_daterange_hours(void *range)
{
    if (!range) return 0;
    rt_daterange_impl *r = (rt_daterange_impl *)range;
    return (r->end - r->start) / 3600;
}

int64_t rt_daterange_duration(void *range)
{
    if (!range) return 0;
    rt_daterange_impl *r = (rt_daterange_impl *)range;
    return r->end - r->start;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

rt_string rt_daterange_to_string(void *range)
{
    if (!range) return rt_string_from_bytes("", 0);
    rt_daterange_impl *r = (rt_daterange_impl *)range;

    char buf[128];
    time_t st = (time_t)r->start;
    time_t et = (time_t)r->end;
    struct tm ts, te;
    gmtime_r(&st, &ts);
    gmtime_r(&et, &te);

    int len = snprintf(buf, sizeof(buf),
                       "%04d-%02d-%02d %02d:%02d - %04d-%02d-%02d %02d:%02d",
                       ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday,
                       ts.tm_hour, ts.tm_min,
                       te.tm_year + 1900, te.tm_mon + 1, te.tm_mday,
                       te.tm_hour, te.tm_min);

    if (len < 0) len = 0;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}
