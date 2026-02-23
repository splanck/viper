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

int64_t rt_duration_from_millis(int64_t ms)
{
    return ms;
}

int64_t rt_duration_from_seconds(int64_t seconds)
{
    return seconds * MS_PER_SECOND;
}

int64_t rt_duration_from_minutes(int64_t minutes)
{
    return minutes * MS_PER_MINUTE;
}

int64_t rt_duration_from_hours(int64_t hours)
{
    return hours * MS_PER_HOUR;
}

int64_t rt_duration_from_days(int64_t days)
{
    return days * MS_PER_DAY;
}

int64_t rt_duration_create(
    int64_t days, int64_t hours, int64_t minutes, int64_t seconds, int64_t millis)
{
    return days * MS_PER_DAY + hours * MS_PER_HOUR + minutes * MS_PER_MINUTE +
           seconds * MS_PER_SECOND + millis;
}

//=============================================================================
// Duration Total Conversions
//=============================================================================

int64_t rt_duration_total_millis(int64_t duration)
{
    return duration;
}

int64_t rt_duration_total_seconds(int64_t duration)
{
    return duration / MS_PER_SECOND;
}

int64_t rt_duration_total_minutes(int64_t duration)
{
    return duration / MS_PER_MINUTE;
}

int64_t rt_duration_total_hours(int64_t duration)
{
    return duration / MS_PER_HOUR;
}

int64_t rt_duration_total_days(int64_t duration)
{
    return duration / MS_PER_DAY;
}

double rt_duration_total_seconds_f(int64_t duration)
{
    return (double)duration / (double)MS_PER_SECOND;
}

//=============================================================================
// Duration Components
//=============================================================================

int64_t rt_duration_get_days(int64_t duration)
{
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return abs_dur / MS_PER_DAY;
}

int64_t rt_duration_get_hours(int64_t duration)
{
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return (abs_dur % MS_PER_DAY) / MS_PER_HOUR;
}

int64_t rt_duration_get_minutes(int64_t duration)
{
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return (abs_dur % MS_PER_HOUR) / MS_PER_MINUTE;
}

int64_t rt_duration_get_seconds(int64_t duration)
{
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return (abs_dur % MS_PER_MINUTE) / MS_PER_SECOND;
}

int64_t rt_duration_get_millis(int64_t duration)
{
    int64_t abs_dur = duration >= 0 ? duration : -duration;
    return abs_dur % MS_PER_SECOND;
}

//=============================================================================
// Duration Operations
//=============================================================================

int64_t rt_duration_add(int64_t d1, int64_t d2)
{
    return d1 + d2;
}

int64_t rt_duration_sub(int64_t d1, int64_t d2)
{
    return d1 - d2;
}

int64_t rt_duration_mul(int64_t duration, int64_t factor)
{
    return duration * factor;
}

int64_t rt_duration_div(int64_t duration, int64_t divisor)
{
    if (divisor == 0)
        return 0; // Avoid division by zero
    return duration / divisor;
}

int64_t rt_duration_abs(int64_t duration)
{
    return duration >= 0 ? duration : (int64_t)(-(uint64_t)duration);
}

int64_t rt_duration_neg(int64_t duration)
{
    return (int64_t)(-(uint64_t)duration);
}

//=============================================================================
// Duration Comparison
//=============================================================================

int64_t rt_duration_cmp(int64_t d1, int64_t d2)
{
    if (d1 < d2)
        return -1;
    if (d1 > d2)
        return 1;
    return 0;
}

//=============================================================================
// Duration Formatting
//=============================================================================

rt_string rt_duration_to_string(int64_t duration)
{
    char buffer[64];

    int negative = duration < 0;
    int64_t abs_dur = negative ? -duration : duration;

    int64_t days = abs_dur / MS_PER_DAY;
    int64_t hours = (abs_dur % MS_PER_DAY) / MS_PER_HOUR;
    int64_t minutes = (abs_dur % MS_PER_HOUR) / MS_PER_MINUTE;
    int64_t seconds = (abs_dur % MS_PER_MINUTE) / MS_PER_SECOND;
    int64_t millis = abs_dur % MS_PER_SECOND;

    const char *sign = negative ? "-" : "";

    if (days > 0)
    {
        if (millis > 0)
        {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s%lld.%02lld:%02lld:%02lld.%03lld",
                     sign,
                     (long long)days,
                     (long long)hours,
                     (long long)minutes,
                     (long long)seconds,
                     (long long)millis);
        }
        else
        {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s%lld.%02lld:%02lld:%02lld",
                     sign,
                     (long long)days,
                     (long long)hours,
                     (long long)minutes,
                     (long long)seconds);
        }
    }
    else
    {
        if (millis > 0)
        {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s%02lld:%02lld:%02lld.%03lld",
                     sign,
                     (long long)hours,
                     (long long)minutes,
                     (long long)seconds,
                     (long long)millis);
        }
        else
        {
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

rt_string rt_duration_to_iso(int64_t duration)
{
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
    if (days > 0)
    {
        p += snprintf(p, (size_t)(end - p), "%lldD", (long long)days);
    }

    if (hours > 0 || minutes > 0 || seconds > 0 || millis > 0)
    {
        *p++ = 'T';
        if (hours > 0)
        {
            p += snprintf(p, (size_t)(end - p), "%lldH", (long long)hours);
        }
        if (minutes > 0)
        {
            p += snprintf(p, (size_t)(end - p), "%lldM", (long long)minutes);
        }
        if (seconds > 0 || millis > 0)
        {
            if (millis > 0)
            {
                p += snprintf(
                    p, (size_t)(end - p), "%lld.%03lldS", (long long)seconds, (long long)millis);
            }
            else
            {
                p += snprintf(p, (size_t)(end - p), "%lldS", (long long)seconds);
            }
        }
    }

    // Handle zero duration
    if (p == buffer + 1 || (p == buffer + 2 && negative))
    {
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

int64_t rt_duration_zero(void)
{
    return 0;
}
