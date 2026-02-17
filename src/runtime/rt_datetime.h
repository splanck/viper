//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_datetime.h
// Purpose: Runtime functions for date/time operations.
// Key invariants: Timestamps are Unix timestamps in seconds since epoch (UTC).
// Ownership/Lifetime: Returned strings are allocated and must be managed by caller.
// Links: src/il/runtime/classes/RuntimeClasses.inc
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Get current Unix timestamp in seconds.
    /// @return Seconds since Unix epoch (1970-01-01 00:00:00 UTC).
    int64_t rt_datetime_now(void);

    /// @brief Get current time in milliseconds.
    /// @return Milliseconds since Unix epoch.
    int64_t rt_datetime_now_ms(void);

    /// @brief Extract year from timestamp.
    /// @param timestamp Unix timestamp in seconds.
    /// @return Year (e.g., 2025).
    int64_t rt_datetime_year(int64_t timestamp);

    /// @brief Extract month from timestamp.
    /// @param timestamp Unix timestamp in seconds.
    /// @return Month (1-12).
    int64_t rt_datetime_month(int64_t timestamp);

    /// @brief Extract day of month from timestamp.
    /// @param timestamp Unix timestamp in seconds.
    /// @return Day (1-31).
    int64_t rt_datetime_day(int64_t timestamp);

    /// @brief Extract hour from timestamp.
    /// @param timestamp Unix timestamp in seconds.
    /// @return Hour (0-23).
    int64_t rt_datetime_hour(int64_t timestamp);

    /// @brief Extract minute from timestamp.
    /// @param timestamp Unix timestamp in seconds.
    /// @return Minute (0-59).
    int64_t rt_datetime_minute(int64_t timestamp);

    /// @brief Extract second from timestamp.
    /// @param timestamp Unix timestamp in seconds.
    /// @return Second (0-59).
    int64_t rt_datetime_second(int64_t timestamp);

    /// @brief Get day of week from timestamp.
    /// @param timestamp Unix timestamp in seconds.
    /// @return Day of week (0=Sunday, 1=Monday, ..., 6=Saturday).
    int64_t rt_datetime_day_of_week(int64_t timestamp);

    /// @brief Format timestamp using strftime format string.
    /// @param timestamp Unix timestamp in seconds.
    /// @param format strftime-compatible format string.
    /// @return Formatted date/time string.
    rt_string rt_datetime_format(int64_t timestamp, rt_string format);

    /// @brief Convert timestamp to ISO 8601 format (UTC).
    /// @param timestamp Unix timestamp in seconds.
    /// @return ISO 8601 formatted string (e.g., "2025-12-05T14:30:00Z").
    rt_string rt_datetime_to_iso(int64_t timestamp);

    /// @brief Convert timestamp to local ISO 8601 format (no Z suffix).
    /// @param timestamp Unix timestamp in seconds.
    /// @return Local ISO 8601 formatted string (e.g., "2025-12-05T14:30:00").
    rt_string rt_datetime_to_local(int64_t timestamp);

    /// @brief Create timestamp from date/time components.
    /// @param year Year (e.g., 2025).
    /// @param month Month (1-12).
    /// @param day Day (1-31).
    /// @param hour Hour (0-23).
    /// @param minute Minute (0-59).
    /// @param second Second (0-59).
    /// @return Unix timestamp in seconds.
    int64_t rt_datetime_create(
        int64_t year, int64_t month, int64_t day, int64_t hour, int64_t minute, int64_t second);

    /// @brief Add seconds to timestamp.
    /// @param timestamp Base timestamp in seconds.
    /// @param seconds Seconds to add (can be negative).
    /// @return New timestamp.
    int64_t rt_datetime_add_seconds(int64_t timestamp, int64_t seconds);

    /// @brief Add days to timestamp.
    /// @param timestamp Base timestamp in seconds.
    /// @param days Days to add (can be negative).
    /// @return New timestamp.
    int64_t rt_datetime_add_days(int64_t timestamp, int64_t days);

    /// @brief Calculate difference between two timestamps.
    /// @param ts1 First timestamp in seconds.
    /// @param ts2 Second timestamp in seconds.
    /// @return Difference (ts1 - ts2) in seconds.
    int64_t rt_datetime_diff(int64_t ts1, int64_t ts2);

    //=========================================================================
    // Parsing Functions
    //=========================================================================

    /// @brief Parse an ISO 8601 datetime string to timestamp.
    /// @param s String like "2024-01-15T10:30:00" or "2024-01-15T10:30:00Z".
    /// @return Unix timestamp, or 0 on parse failure.
    int64_t rt_datetime_parse_iso(rt_string s);

    /// @brief Parse a date string to timestamp (midnight).
    /// @param s String like "2024-01-15".
    /// @return Unix timestamp at midnight, or 0 on parse failure.
    int64_t rt_datetime_parse_date(rt_string s);

    /// @brief Parse a time string to seconds since midnight.
    /// @param s String like "10:30:00" or "10:30".
    /// @return Seconds since midnight, or -1 on parse failure.
    int64_t rt_datetime_parse_time(rt_string s);

    /// @brief Try to parse a datetime string in any supported format.
    /// @param s Input string (ISO, date-only, or time-only).
    /// @return Unix timestamp, or 0 on parse failure.
    int64_t rt_datetime_try_parse(rt_string s);

#ifdef __cplusplus
}
#endif
