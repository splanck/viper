//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_daterange.h
// Purpose: Date interval type representing a range between two Unix timestamps, with containment
// testing, overlap detection, and duration queries.
//
// Key invariants:
//   - Start and end are Unix timestamps in seconds since epoch (UTC).
//   - Start must be <= End; violating this constraint produces undefined behavior.
//   - Contains check is inclusive on both ends.
//   - Duration returns end - start in seconds; may be zero for point ranges.
//
// Ownership/Lifetime:
//   - DateRange objects are GC-managed opaque pointers.
//   - Callers must not free range objects directly.
//
// Links: src/runtime/core/rt_daterange.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a date range from start and end timestamps.
    /// @param start Start timestamp in seconds.
    /// @param end End timestamp in seconds.
    /// @return Date range object.
    void *rt_daterange_new(int64_t start, int64_t end);

    /// @brief Get start timestamp.
    /// @param range Date range object.
    /// @return Start timestamp in seconds.
    int64_t rt_daterange_start(void *range);

    /// @brief Get end timestamp.
    /// @param range Date range object.
    /// @return End timestamp in seconds.
    int64_t rt_daterange_end(void *range);

    /// @brief Check if a timestamp falls within the range (inclusive).
    /// @param range Date range object.
    /// @param timestamp Timestamp to check.
    /// @return true if contained, false otherwise.
    bool rt_daterange_contains(void *range, int64_t timestamp);

    /// @brief Check if two ranges overlap.
    /// @param range First date range.
    /// @param other Second date range.
    /// @return true if they overlap, false otherwise.
    bool rt_daterange_overlaps(void *range, void *other);

    /// @brief Get the intersection of two ranges.
    /// @param range First date range.
    /// @param other Second date range.
    /// @return Intersection range, or NULL if no overlap.
    void *rt_daterange_intersection(void *range, void *other);

    /// @brief Get the union of two ranges.
    /// @details Returns NULL if ranges are not contiguous or overlapping.
    /// @param range First date range.
    /// @param other Second date range.
    /// @return Union range, or NULL if not contiguous.
    void *rt_daterange_union_range(void *range, void *other);

    /// @brief Get the number of days in the range.
    /// @param range Date range object.
    /// @return Number of days (rounded down).
    int64_t rt_daterange_days(void *range);

    /// @brief Get the number of hours in the range.
    /// @param range Date range object.
    /// @return Number of hours (rounded down).
    int64_t rt_daterange_hours(void *range);

    /// @brief Get the duration of the range in seconds.
    /// @param range Date range object.
    /// @return Duration in seconds.
    int64_t rt_daterange_duration(void *range);

    /// @brief Format range as a string.
    /// @param range Date range object.
    /// @return String like "2025-01-01 00:00 - 2025-01-31 23:59".
    rt_string rt_daterange_to_string(void *range);

#ifdef __cplusplus
}
#endif
