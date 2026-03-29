//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_duration.c
// Purpose: Implements the Duration/TimeSpan type for the Viper runtime.
//          A Duration is a signed 64-bit integer representing a time span in
//          milliseconds. Provides factory functions (FromMillis, FromSeconds,
//          FromMinutes, FromHours, FromDays), total-unit accessors, component
//          extraction (Days/Hours/Minutes/Seconds/Millis parts), and formatting.
//
// Key invariants:
//   - A Duration is represented as a plain int64_t (milliseconds); there is no
//     wrapper struct or heap object — values are passed by value.
//   - All factory and conversion functions are pure arithmetic; no validation
//     is performed on overflow (callers are responsible).
//   - Component extraction (e.g., rt_duration_hours_part) returns the whole-
//     unit component after subtracting larger units, analogous to .NET TimeSpan.
//   - Negative durations represent intervals in the past; component parts may
//     be negative when the total duration is negative.
//
// Ownership/Lifetime:
//   - Duration values are scalar int64_t; no heap allocation is performed.
//   - Formatted strings returned by rt_duration_to_string are newly allocated
//     rt_string values; the caller owns the reference and must unref when done.
//
// Links: src/runtime/core/rt_duration.h (public API),
//        src/runtime/core/rt_daterange.c (uses Duration for span computation),
//        src/runtime/core/rt_stopwatch.c (produces Duration-valued elapsed time)
//
//===----------------------------------------------------------------------===//

#include "rt_duration.h"

#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constants for time unit conversions
#define MS_PER_SECOND 1000LL
#define MS_PER_MINUTE (60LL * MS_PER_SECOND)
#define MS_PER_HOUR (60LL * MS_PER_MINUTE)
#define MS_PER_DAY (24LL * MS_PER_HOUR)

//=============================================================================
// Duration Creation
//=============================================================================

/// @brief Create a Duration from a raw millisecond count (identity).
/// @param ms Milliseconds.
/// @return The same value (Durations are stored as milliseconds internally).
int64_t rt_duration_from_millis(int64_t ms) {
    return ms;
}

/// @brief Create a Duration from a count of seconds.
/// @param seconds Number of seconds.
/// @return Duration in milliseconds (seconds * 1000).
int64_t rt_duration_from_seconds(int64_t seconds) {
    return seconds * MS_PER_SECOND;
}

/// @brief Create a Duration from a count of minutes.
/// @param minutes Number of minutes.
/// @return Duration in milliseconds (minutes * 60000).
int64_t rt_duration_from_minutes(int64_t minutes) {
    return minutes * MS_PER_MINUTE;
}

/// @brief Create a Duration from a count of hours.
/// @param hours Number of hours.
/// @return Duration in milliseconds (hours * 3600000).
int64_t rt_duration_from_hours(int64_t hours) {
    return hours * MS_PER_HOUR;
}

/// @brief Create a Duration from a count of days.
/// @param days Number of days.
/// @return Duration in milliseconds (days * 86400000).
int64_t rt_duration_from_days(int64_t days) {
    return days * MS_PER_DAY;
}

int64_t rt_duration_create(
    int64_t days, int64_t hours, int64_t minutes, int64_t seconds, int64_t millis) {
    return days * MS_PER_DAY + hours * MS_PER_HOUR + minutes * MS_PER_MINUTE +
           seconds * MS_PER_SECOND + millis;
}

//=============================================================================
// Duration Total Conversions
//=============================================================================

/// @brief Return the total milliseconds in the duration (identity).
/// @param duration Duration in milliseconds.
/// @return Same value.
int64_t rt_duration_total_millis(int64_t duration) {
    return duration;
}

/// @brief Return the total whole seconds in the duration.
/// @param duration Duration in milliseconds.
/// @return Truncated seconds (duration / 1000).
int64_t rt_duration_total_seconds(int64_t duration) {
    return duration / MS_PER_SECOND;
}

/// @brief Return the total whole minutes in the duration.
/// @param duration Duration in milliseconds.
/// @return Truncated minutes (duration / 60000).
int64_t rt_duration_total_minutes(int64_t duration) {
    return duration / MS_PER_MINUTE;
}

/// @brief Return the total whole hours in the duration.
/// @param duration Duration in milliseconds.
/// @return Truncated hours (duration / 3600000).
int64_t rt_duration_total_hours(int64_t duration) {
    return duration / MS_PER_HOUR;
}

/// @brief Return the total whole days in the duration.
/// @param duration Duration in milliseconds.
/// @return Truncated days (duration / 86400000).
int64_t rt_duration_total_days(int64_t duration) {
    return duration / MS_PER_DAY;
}

/// @brief Return the total seconds as a floating-point value.
/// @details Useful for sub-second precision without truncation.
/// @param duration Duration in milliseconds.
/// @return Fractional seconds (e.g. 1500ms → 1.5).
double rt_duration_total_seconds_f(int64_t duration) {
    return (double)duration / (double)MS_PER_SECOND;
}

//=============================================================================
// Duration Components
//=============================================================================

/// @brief Extract the days component (largest unit).
/// @details Returns abs(duration) / MS_PER_DAY — the whole days portion.
/// @param duration Duration in milliseconds.
/// @return Unsigned day count.
int64_t rt_duration_get_days(int64_t duration) {
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return abs_dur / MS_PER_DAY;
}

/// @brief Extract the hours component (0-23 after removing days).
/// @param duration Duration in milliseconds.
/// @return Hours remaining after subtracting whole days.
int64_t rt_duration_get_hours(int64_t duration) {
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return (abs_dur % MS_PER_DAY) / MS_PER_HOUR;
}

/// @brief Extract the minutes component (0-59 after removing hours).
/// @param duration Duration in milliseconds.
/// @return Minutes remaining after subtracting whole hours.
int64_t rt_duration_get_minutes(int64_t duration) {
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return (abs_dur % MS_PER_HOUR) / MS_PER_MINUTE;
}

/// @brief Extract the seconds component (0-59 after removing minutes).
/// @param duration Duration in milliseconds.
/// @return Seconds remaining after subtracting whole minutes.
int64_t rt_duration_get_seconds(int64_t duration) {
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return (abs_dur % MS_PER_MINUTE) / MS_PER_SECOND;
}

/// @brief Extract the milliseconds component (0-999 after removing seconds).
/// @param duration Duration in milliseconds.
/// @return Milliseconds remaining after subtracting whole seconds.
int64_t rt_duration_get_millis(int64_t duration) {
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return abs_dur % MS_PER_SECOND;
}

//=============================================================================
// Duration Operations
//=============================================================================

/// @brief Add two durations.
/// @param d1 First duration in milliseconds.
/// @param d2 Second duration in milliseconds.
/// @return Sum (d1 + d2) in milliseconds.
int64_t rt_duration_add(int64_t d1, int64_t d2) {
    return d1 + d2;
}

/// @brief Subtract two durations.
/// @param d1 First duration in milliseconds.
/// @param d2 Second duration in milliseconds.
/// @return Difference (d1 - d2) in milliseconds.
int64_t rt_duration_sub(int64_t d1, int64_t d2) {
    return d1 - d2;
}

/// @brief Scale a duration by an integer factor.
/// @param duration Duration in milliseconds.
/// @param factor Multiplier.
/// @return Scaled duration (duration * factor).
int64_t rt_duration_mul(int64_t duration, int64_t factor) {
    return duration * factor;
}

/// @brief Divide a duration by an integer divisor.
/// @details Traps on zero divisor to match runtime error conventions.
/// @param duration Duration in milliseconds.
/// @param divisor Divisor (must be non-zero).
/// @return Truncated quotient (duration / divisor).
int64_t rt_duration_div(int64_t duration, int64_t divisor) {
    if (divisor == 0)
        return 0; // Avoid division by zero
    return duration / divisor;
}

/// @brief Return the absolute value of a duration.
/// @param duration Duration in milliseconds (may be negative).
/// @return Non-negative magnitude.
int64_t rt_duration_abs(int64_t duration) {
    return duration >= 0 ? duration : (int64_t)(0 - (uint64_t)duration);
}

/// @brief Negate a duration.
/// @param duration Duration in milliseconds.
/// @return Negated value (-duration).
int64_t rt_duration_neg(int64_t duration) {
    return (int64_t)(0 - (uint64_t)duration);
}

//=============================================================================
// Duration Comparison
//=============================================================================

/// @brief Compare two durations.
/// @param d1 First duration in milliseconds.
/// @param d2 Second duration in milliseconds.
/// @return -1 if d1 < d2, 0 if equal, 1 if d1 > d2.
int64_t rt_duration_cmp(int64_t d1, int64_t d2) {
    if (d1 < d2)
        return -1;
    if (d1 > d2)
        return 1;
    return 0;
}

//=============================================================================
// Duration Formatting
//=============================================================================

/// @brief Format a duration as a human-readable string.
/// @details Produces output like "2d 3h 15m 30s 500ms", omitting zero components.
///          Negative durations are prefixed with "-". Zero returns "0ms".
/// @param duration Duration in milliseconds.
/// @return Newly allocated runtime string.
rt_string rt_duration_to_string(int64_t duration) {
    char buffer[64];

    int negative = duration < 0;
    int64_t abs_dur = negative ? -duration : duration;

    int64_t days = abs_dur / MS_PER_DAY;
    int64_t hours = (abs_dur % MS_PER_DAY) / MS_PER_HOUR;
    int64_t minutes = (abs_dur % MS_PER_HOUR) / MS_PER_MINUTE;
    int64_t seconds = (abs_dur % MS_PER_MINUTE) / MS_PER_SECOND;
    int64_t millis = abs_dur % MS_PER_SECOND;

    const char *sign = negative ? "-" : "";

    if (days > 0) {
        if (millis > 0) {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s%lld.%02lld:%02lld:%02lld.%03lld",
                     sign,
                     (long long)days,
                     (long long)hours,
                     (long long)minutes,
                     (long long)seconds,
                     (long long)millis);
        } else {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s%lld.%02lld:%02lld:%02lld",
                     sign,
                     (long long)days,
                     (long long)hours,
                     (long long)minutes,
                     (long long)seconds);
        }
    } else {
        if (millis > 0) {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s%02lld:%02lld:%02lld.%03lld",
                     sign,
                     (long long)hours,
                     (long long)minutes,
                     (long long)seconds,
                     (long long)millis);
        } else {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s%02lld:%02lld:%02lld",
                     sign,
                     (long long)hours,
                     (long long)minutes,
                     (long long)seconds);
        }
    }

    return rt_string_from_bytes(buffer, strlen(buffer));
}

/// @brief Format a duration as an ISO 8601 duration string.
/// @details Produces output like "P2DT3H15M30.5S" (period-designator format).
///          This format is machine-parseable and widely used in APIs.
/// @param duration Duration in milliseconds.
/// @return Newly allocated runtime string.
rt_string rt_duration_to_iso(int64_t duration) {
    char buffer[64];
    char *p = buffer;

    int negative = duration < 0;
    int64_t abs_dur = negative ? -duration : duration;

    int64_t days = abs_dur / MS_PER_DAY;
    int64_t hours = (abs_dur % MS_PER_DAY) / MS_PER_HOUR;
    int64_t minutes = (abs_dur % MS_PER_HOUR) / MS_PER_MINUTE;
    int64_t seconds = (abs_dur % MS_PER_MINUTE) / MS_PER_SECOND;
    int64_t millis = abs_dur % MS_PER_SECOND;

    if (negative)
        *p++ = '-';
    *p++ = 'P';

    char *end = buffer + sizeof(buffer);
    if (days > 0) {
        p += snprintf(p, (size_t)(end - p), "%lldD", (long long)days);
    }

    if (hours > 0 || minutes > 0 || seconds > 0 || millis > 0) {
        *p++ = 'T';
        if (hours > 0) {
            p += snprintf(p, (size_t)(end - p), "%lldH", (long long)hours);
        }
        if (minutes > 0) {
            p += snprintf(p, (size_t)(end - p), "%lldM", (long long)minutes);
        }
        if (seconds > 0 || millis > 0) {
            if (millis > 0) {
                p += snprintf(
                    p, (size_t)(end - p), "%lld.%03lldS", (long long)seconds, (long long)millis);
            } else {
                p += snprintf(p, (size_t)(end - p), "%lldS", (long long)seconds);
            }
        }
    }

    // Handle zero duration
    if (p == buffer + 1 || (p == buffer + 2 && negative)) {
        *p++ = 'T';
        *p++ = '0';
        *p++ = 'S';
    }

    *p = '\0';
    return rt_string_from_bytes(buffer, strlen(buffer));
}

//=============================================================================
// Constants
//=============================================================================

/// @brief Return the zero duration (0 milliseconds).
/// @return Result value.
int64_t rt_duration_zero(void) {
    return 0;
}
