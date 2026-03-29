//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_daterange.c
// Purpose: Implements the DateRange type for the Viper runtime, representing
//          a closed interval [start, end] of Unix timestamps (seconds since
//          epoch). Provides construction, containment testing, overlap
//          detection, duration computation, and string formatting.
//
// Key invariants:
//   - The interval is always stored in normalised order: start <= end; if the
//     caller passes start > end the constructor swaps them.
//   - Timestamps are 64-bit signed integers (seconds since Unix epoch); there
//     is no timezone conversion — all values are treated as UTC seconds.
//   - Contains(t) is inclusive on both endpoints: t >= start && t <= end.
//   - Overlaps(a, b) is true when a.start <= b.end && b.start <= a.end.
//   - NULL pointers to range objects cause the corresponding query to return 0
//     or false rather than trapping.
//
// Ownership/Lifetime:
//   - DateRange instances are heap-allocated via rt_obj_new_i64 and managed
//     by the runtime GC; callers do not free them explicitly.
//   - Formatted strings returned by rt_daterange_to_string are newly allocated
//     rt_string values; the caller owns the reference and must unref when done.
//
// Links: src/runtime/core/rt_daterange.h (public API),
//        src/runtime/core/rt_datetime.c (DateTime type),
//        src/runtime/core/rt_duration.c (Duration/TimeSpan type)
//
//===----------------------------------------------------------------------===//

#include "rt_daterange.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_string_builder.h"

#include <stdio.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Internal structure
// ---------------------------------------------------------------------------

typedef struct {
    void *vptr;
    int64_t start; // Unix timestamp in seconds
    int64_t end;   // Unix timestamp in seconds
} rt_daterange_impl;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

/// @brief Create a DateRange representing a closed interval [start, end].
/// @details Automatically normalizes the interval so start <= end by swapping
///          if the caller passes them in reverse order. This prevents invalid
///          ranges from entering the system.
/// @param start Start timestamp in seconds since Unix epoch (UTC).
/// @param end End timestamp in seconds since Unix epoch (UTC).
/// @return New GC-managed DateRange object.
void *rt_daterange_new(int64_t start, int64_t end) {
    // Ensure start <= end
    int64_t s = start <= end ? start : end;
    int64_t e = start <= end ? end : start;

    rt_daterange_impl *r = (rt_daterange_impl *)rt_obj_new_i64(0, sizeof(rt_daterange_impl));
    r->start = s;
    r->end = e;
    return r;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/// @brief Return the start timestamp of the range (seconds since epoch).
/// @param range DateRange object pointer; returns 0 if NULL.
/// @return Start timestamp in UTC seconds.
int64_t rt_daterange_start(void *range) {
    if (!range)
        return 0;
    return ((rt_daterange_impl *)range)->start;
}

/// @brief Return the end timestamp of the range (seconds since epoch).
/// @param range DateRange object pointer; returns 0 if NULL.
/// @return End timestamp in UTC seconds.
int64_t rt_daterange_end(void *range) {
    if (!range)
        return 0;
    return ((rt_daterange_impl *)range)->end;
}

// ---------------------------------------------------------------------------
// Containment / overlap
// ---------------------------------------------------------------------------

/// @brief Test whether a timestamp falls within the range (inclusive).
/// @details Returns true when start <= timestamp <= end. Both endpoints are
///          included because the range represents a closed interval.
/// @param range DateRange object pointer; returns false if NULL.
/// @param timestamp Unix timestamp (UTC seconds) to test.
/// @return 1 if the timestamp is within [start, end], 0 otherwise.
int8_t rt_daterange_contains(void *range, int64_t timestamp) {
    if (!range)
        return false;
    rt_daterange_impl *r = (rt_daterange_impl *)range;
    return (timestamp >= r->start && timestamp <= r->end);
}

/// @brief Test whether two ranges share any common time.
/// @details Two closed intervals [a.start, a.end] and [b.start, b.end] overlap
///          when a.start <= b.end AND b.start <= a.end. This handles all cases:
///          partial overlap, containment, and touching endpoints.
/// @param range First DateRange; returns false if NULL.
/// @param other Second DateRange; returns false if NULL.
/// @return 1 if the ranges overlap, 0 otherwise.
int8_t rt_daterange_overlaps(void *range, void *other) {
    if (!range || !other)
        return false;
    rt_daterange_impl *a = (rt_daterange_impl *)range;
    rt_daterange_impl *b = (rt_daterange_impl *)other;
    return (a->start <= b->end && b->start <= a->end);
}

// ---------------------------------------------------------------------------
// Set operations
// ---------------------------------------------------------------------------

/// @brief Return the overlapping portion of two ranges, or NULL if disjoint.
/// @details The intersection start is max(a.start, b.start) and the end is
///          min(a.end, b.end). If start > end, there is no overlap and NULL
///          is returned. Otherwise a new DateRange covering the overlap is created.
/// @param range First DateRange.
/// @param other Second DateRange.
/// @return New DateRange for the overlap, or NULL if the ranges are disjoint.
void *rt_daterange_intersection(void *range, void *other) {
    if (!range || !other)
        return NULL;
    rt_daterange_impl *a = (rt_daterange_impl *)range;
    rt_daterange_impl *b = (rt_daterange_impl *)other;

    int64_t s = a->start > b->start ? a->start : b->start;
    int64_t e = a->end < b->end ? a->end : b->end;

    if (s > e)
        return NULL; // no overlap
    return rt_daterange_new(s, e);
}

/// @brief Merge two ranges into a single range, or NULL if they have a gap.
/// @details The union is only defined when the ranges overlap or are contiguous
///          (within 1 second of each other). If there is a gap, merging would
///          create a range that includes time not covered by either input, so
///          NULL is returned instead. The result spans min(starts) to max(ends).
/// @param range First DateRange.
/// @param other Second DateRange.
/// @return New DateRange spanning both, or NULL if there is a gap between them.
void *rt_daterange_union_range(void *range, void *other) {
    if (!range || !other)
        return NULL;
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

/// @brief Return the number of whole days spanned by the range.
/// @details Computed as (end - start) / 86400. Fractional days are truncated.
///          For a 36-hour range, this returns 1 (not 2).
/// @param range DateRange object pointer; returns 0 if NULL.
/// @return Whole days contained in the range.
int64_t rt_daterange_days(void *range) {
    if (!range)
        return 0;
    rt_daterange_impl *r = (rt_daterange_impl *)range;
    return (r->end - r->start) / 86400;
}

/// @brief Return the number of whole hours spanned by the range.
/// @details Computed as (end - start) / 3600. Fractional hours are truncated.
/// @param range DateRange object pointer; returns 0 if NULL.
/// @return Whole hours contained in the range.
int64_t rt_daterange_hours(void *range) {
    if (!range)
        return 0;
    rt_daterange_impl *r = (rt_daterange_impl *)range;
    return (r->end - r->start) / 3600;
}

/// @brief Return the exact duration of the range in seconds.
/// @details Simply end - start. This is the raw difference without rounding,
///          suitable for precise timing. For human-friendly units, use
///          rt_daterange_days or rt_daterange_hours.
/// @param range DateRange object pointer; returns 0 if NULL.
/// @return Duration in seconds (>= 0).
int64_t rt_daterange_duration(void *range) {
    if (!range)
        return 0;
    rt_daterange_impl *r = (rt_daterange_impl *)range;
    return r->end - r->start;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

/// @brief Format the range as "YYYY-MM-DD HH:MM - YYYY-MM-DD HH:MM" (UTC).
/// @details Converts both timestamps to UTC calendar components via gmtime_r
///          and formats into a fixed-layout string. Returns an empty string for
///          NULL inputs. The output is always in UTC with no timezone suffix.
/// @param range DateRange object pointer.
/// @return Newly allocated runtime string with the formatted range.
rt_string rt_daterange_to_string(void *range) {
    if (!range)
        return rt_string_from_bytes("", 0);
    rt_daterange_impl *r = (rt_daterange_impl *)range;

    char buf[128];
    time_t st = (time_t)r->start;
    time_t et = (time_t)r->end;
    struct tm ts, te;
    rt_gmtime_r(&st, &ts);
    rt_gmtime_r(&et, &te);

    int len = snprintf(buf,
                       sizeof(buf),
                       "%04d-%02d-%02d %02d:%02d - %04d-%02d-%02d %02d:%02d",
                       ts.tm_year + 1900,
                       ts.tm_mon + 1,
                       ts.tm_mday,
                       ts.tm_hour,
                       ts.tm_min,
                       te.tm_year + 1900,
                       te.tm_mon + 1,
                       te.tm_mday,
                       te.tm_hour,
                       te.tm_min);

    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}
