//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_datetime.c
/// @brief Date and time operations for the Viper.DateTime class.
///
/// This file implements functions for working with dates and times, including
/// getting the current time, extracting date/time components, formatting,
/// and arithmetic operations.
///
/// **Time Representation:**
/// Dates and times are represented as Unix timestamps - the number of seconds
/// since the Unix epoch (January 1, 1970, 00:00:00 UTC). This provides a
/// consistent, timezone-independent representation of moments in time.
///
/// ```
/// Unix Epoch (0)        Now                   Far Future
///     │                  │                        │
///     ├──────────────────┼────────────────────────┤
///     │                  │                        │
///  Jan 1, 1970      Current Time           Jan 19, 2038
///  00:00:00 UTC                           (32-bit limit)
/// ```
///
/// **Component Extraction:**
/// Timestamps can be decomposed into human-readable components:
/// ```
/// Timestamp: 1703001600
///     │
///     ├──► Year:   2023
///     ├──► Month:  12 (December)
///     ├──► Day:    19
///     ├──► Hour:   16 (4 PM)
///     ├──► Minute: 0
///     ├──► Second: 0
///     └──► DayOfWeek: 2 (Tuesday)
/// ```
///
/// **Day of Week Values:**
/// | Value | Day       |
/// |-------|-----------|
/// | 0     | Sunday    |
/// | 1     | Monday    |
/// | 2     | Tuesday   |
/// | 3     | Wednesday |
/// | 4     | Thursday  |
/// | 5     | Friday    |
/// | 6     | Saturday  |
///
/// **Time Zones:**
/// - Component extraction (Year, Month, etc.) uses local time zone
/// - ISO format output uses UTC (ends with 'Z')
/// - Timestamps themselves are timezone-independent
///
/// **Common Use Cases:**
/// - Displaying current date/time to users
/// - Logging with timestamps
/// - Calculating durations between events
/// - Scheduling future events
/// - Formatting dates for different locales
///
/// **Thread Safety:** All functions use thread-safe time conversion functions
/// (rt_localtime_r/rt_gmtime_r) that store results in caller-provided buffers,
/// making them safe for use from multiple threads concurrently.
///
/// @see rt_time.c For high-resolution timing and performance measurement
/// @see rt_stopwatch.c For elapsed time measurement
///
//===----------------------------------------------------------------------===//

#include "rt_datetime.h"
#include "rt_platform.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if RT_PLATFORM_MACOS
#include <sys/time.h>
#endif

/// @brief Gets the current date/time as a Unix timestamp.
///
/// Returns the current time as the number of seconds elapsed since the Unix
/// epoch (January 1, 1970, 00:00:00 UTC). This timestamp can be used with
/// other DateTime functions to extract components or perform arithmetic.
///
/// **Usage example:**
/// ```
/// Dim now = DateTime.Now()
/// Print "Current timestamp: " & now
/// Print "Year: " & DateTime.Year(now)
/// Print "Month: " & DateTime.Month(now)
/// Print "Day: " & DateTime.Day(now)
/// ```
///
/// @return The current Unix timestamp in seconds. Returns 0 if the system
///         time cannot be determined (extremely rare).
///
/// @note O(1) time complexity - single system call.
/// @note Resolution is seconds. Use rt_datetime_now_ms for milliseconds.
/// @note The timestamp is timezone-independent (represents an instant in time).
///
/// @see rt_datetime_now_ms For millisecond precision
/// @see rt_datetime_year For extracting the year component
int64_t rt_datetime_now(void)
{
    return (int64_t)time(NULL);
}

/// @brief Gets the current date/time as milliseconds since the Unix epoch.
///
/// Returns the current time with millisecond precision - the number of
/// milliseconds elapsed since the Unix epoch (January 1, 1970, 00:00:00 UTC).
/// This provides higher precision than rt_datetime_now() which only has
/// second resolution.
///
/// **Precision comparison:**
/// ```
/// DateTime.Now()   = 1703001600       (second precision)
/// DateTime.NowMs() = 1703001600123    (millisecond precision)
///                              ^^^
///                          milliseconds
/// ```
///
/// **Usage example:**
/// ```
/// Dim startMs = DateTime.NowMs()
/// ' ... do some work ...
/// Dim endMs = DateTime.NowMs()
/// Print "Operation took " & (endMs - startMs) & " milliseconds"
/// ```
///
/// **Note:** For performance timing, consider using Stopwatch instead, which
/// uses a monotonic clock that isn't affected by system time changes.
///
/// @return The current time in milliseconds since the Unix epoch.
///
/// @note O(1) time complexity - single system call.
/// @note Uses gettimeofday on macOS, clock_gettime on other platforms.
/// @note Unlike Stopwatch, this is wall-clock time and can be affected by
///       system time adjustments (NTP, manual changes, etc.).
///
/// @see rt_datetime_now For second-precision timestamps
/// @see rt_stopwatch.c For monotonic elapsed time measurement
int64_t rt_datetime_now_ms(void)
{
#if RT_PLATFORM_WINDOWS
    return rt_windows_time_ms();
#elif RT_PLATFORM_VIPERDOS
    // ViperDOS provides clock_gettime via libc.
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
    }
#elif RT_PLATFORM_MACOS
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

/// @brief Extracts the year component from a timestamp.
///
/// Returns the four-digit year (e.g., 2023) from a Unix timestamp, interpreted
/// in the local time zone.
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
/// Dim year = DateTime.Year(now)  ' e.g., 2023
/// Print "Current year: " & year
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
///
/// @return The year (e.g., 2023), or 0 if the timestamp is invalid.
///
/// @note Uses local time zone for conversion.
/// @note O(1) time complexity.
///
/// @see rt_datetime_month For extracting the month
/// @see rt_datetime_day For extracting the day
int64_t rt_datetime_year(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    return tm ? (int64_t)(tm->tm_year + 1900) : 0;
}

/// @brief Extracts the month component from a timestamp.
///
/// Returns the month (1-12) from a Unix timestamp, interpreted in the local
/// time zone.
///
/// **Month values:**
/// | Value | Month     |
/// |-------|-----------|
/// | 1     | January   |
/// | 2     | February  |
/// | 3     | March     |
/// | ...   | ...       |
/// | 12    | December  |
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
/// Dim month = DateTime.Month(now)  ' 1-12
/// Dim names() = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}
/// Print "Current month: " & names(month - 1)
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
///
/// @return The month (1-12), or 0 if the timestamp is invalid.
///
/// @note Uses local time zone for conversion.
/// @note Returns 1-based month (January = 1), unlike C's tm_mon which is 0-based.
/// @note O(1) time complexity.
///
/// @see rt_datetime_year For extracting the year
/// @see rt_datetime_day For extracting the day
int64_t rt_datetime_month(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    return tm ? (int64_t)(tm->tm_mon + 1) : 0;
}

/// @brief Extracts the day of month component from a timestamp.
///
/// Returns the day of the month (1-31) from a Unix timestamp, interpreted in
/// the local time zone.
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
/// Dim day = DateTime.Day(now)  ' 1-31
/// Print "Today is day " & day & " of the month"
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
///
/// @return The day of month (1-31), or 0 if the timestamp is invalid.
///
/// @note Uses local time zone for conversion.
/// @note Valid range depends on the month (28-31 days).
/// @note O(1) time complexity.
///
/// @see rt_datetime_month For extracting the month
/// @see rt_datetime_day_of_week For getting the weekday
int64_t rt_datetime_day(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    return tm ? (int64_t)tm->tm_mday : 0;
}

/// @brief Extracts the hour component from a timestamp.
///
/// Returns the hour (0-23) in 24-hour format from a Unix timestamp, interpreted
/// in the local time zone.
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
/// Dim hour = DateTime.Hour(now)  ' 0-23
/// If hour < 12 Then
///     Print "Good morning!"
/// ElseIf hour < 17 Then
///     Print "Good afternoon!"
/// Else
///     Print "Good evening!"
/// End If
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
///
/// @return The hour (0-23), or 0 if the timestamp is invalid.
///
/// @note Uses local time zone for conversion.
/// @note Returns 24-hour format (0 = midnight, 12 = noon, 23 = 11 PM).
/// @note O(1) time complexity.
///
/// @see rt_datetime_minute For extracting minutes
/// @see rt_datetime_second For extracting seconds
int64_t rt_datetime_hour(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    return tm ? (int64_t)tm->tm_hour : 0;
}

/// @brief Extracts the minute component from a timestamp.
///
/// Returns the minute (0-59) from a Unix timestamp, interpreted in the local
/// time zone.
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
/// Dim h = DateTime.Hour(now)
/// Dim m = DateTime.Minute(now)
/// Print "Current time: " & h & ":" & Format(m, "00")
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
///
/// @return The minute (0-59), or 0 if the timestamp is invalid.
///
/// @note Uses local time zone for conversion.
/// @note O(1) time complexity.
///
/// @see rt_datetime_hour For extracting the hour
/// @see rt_datetime_second For extracting seconds
int64_t rt_datetime_minute(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    return tm ? (int64_t)tm->tm_min : 0;
}

/// @brief Extracts the second component from a timestamp.
///
/// Returns the second (0-59) from a Unix timestamp, interpreted in the local
/// time zone. In rare cases during leap seconds, the value might be 60.
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
/// Dim s = DateTime.Second(now)  ' 0-59
/// Print "Seconds: " & s
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
///
/// @return The second (0-59, rarely 60 for leap seconds), or 0 if invalid.
///
/// @note Uses local time zone for conversion.
/// @note O(1) time complexity.
///
/// @see rt_datetime_minute For extracting minutes
/// @see rt_datetime_hour For extracting the hour
int64_t rt_datetime_second(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    return tm ? (int64_t)tm->tm_sec : 0;
}

/// @brief Extracts the day of week from a timestamp.
///
/// Returns the day of the week (0-6) from a Unix timestamp, interpreted in
/// the local time zone. Sunday is 0, Saturday is 6.
///
/// **Day of week values:**
/// | Value | Day       |
/// |-------|-----------|
/// | 0     | Sunday    |
/// | 1     | Monday    |
/// | 2     | Tuesday   |
/// | 3     | Wednesday |
/// | 4     | Thursday  |
/// | 5     | Friday    |
/// | 6     | Saturday  |
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
/// Dim dow = DateTime.DayOfWeek(now)
/// Dim names() = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}
/// Print "Today is " & names(dow)
///
/// If dow = 0 Or dow = 6 Then
///     Print "It's the weekend!"
/// Else
///     Print "It's a weekday."
/// End If
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
///
/// @return The day of week (0 = Sunday, 6 = Saturday), or 0 if invalid.
///
/// @note Uses local time zone for conversion.
/// @note O(1) time complexity.
///
/// @see rt_datetime_day For the day of month
int64_t rt_datetime_day_of_week(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    return tm ? (int64_t)tm->tm_wday : 0;
}

/// @brief Formats a timestamp using a strftime format string.
///
/// Converts a Unix timestamp to a formatted string representation using the
/// C strftime() format specifiers. This provides flexible, locale-aware date
/// and time formatting.
///
/// **Common format specifiers:**
/// | Specifier | Description              | Example    |
/// |-----------|--------------------------|------------|
/// | %Y        | 4-digit year             | 2023       |
/// | %m        | Month (01-12)            | 12         |
/// | %d        | Day of month (01-31)     | 19         |
/// | %H        | Hour 24h (00-23)         | 14         |
/// | %M        | Minute (00-59)           | 30         |
/// | %S        | Second (00-59)           | 45         |
/// | %I        | Hour 12h (01-12)         | 02         |
/// | %p        | AM/PM                    | PM         |
/// | %A        | Full weekday name        | Tuesday    |
/// | %a        | Abbreviated weekday      | Tue        |
/// | %B        | Full month name          | December   |
/// | %b        | Abbreviated month        | Dec        |
/// | %%        | Literal %                | %          |
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
///
/// ' Standard formats
/// Print DateTime.Format(now, "%Y-%m-%d")           ' 2023-12-19
/// Print DateTime.Format(now, "%H:%M:%S")           ' 14:30:45
/// Print DateTime.Format(now, "%Y-%m-%d %H:%M:%S")  ' 2023-12-19 14:30:45
///
/// ' Human-readable
/// Print DateTime.Format(now, "%A, %B %d, %Y")      ' Tuesday, December 19, 2023
/// Print DateTime.Format(now, "%I:%M %p")           ' 02:30 PM
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
/// @param format Viper string containing strftime format specifiers.
///
/// @return Formatted date/time string, or empty string if:
///         - The timestamp is invalid
///         - The format string is NULL or empty
///         - The formatted output exceeds 256 characters
///
/// @note Uses local time zone for conversion.
/// @note Format specifiers are locale-dependent (month/day names).
/// @note Maximum output length is 256 characters.
/// @note O(1) time complexity.
///
/// @see rt_datetime_to_iso For ISO 8601 format (UTC)
rt_string rt_datetime_format(int64_t timestamp, rt_string format)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    if (!tm)
    {
        return rt_string_from_bytes("", 0);
    }

    // Get format string as C string
    const char *fmt_cstr = rt_string_cstr(format);
    if (!fmt_cstr || *fmt_cstr == '\0')
    {
        return rt_string_from_bytes("", 0);
    }

    // Format the time
    char buffer[256];
    size_t len = strftime(buffer, sizeof(buffer), fmt_cstr, tm);

    if (len == 0)
    {
        return rt_string_from_bytes("", 0);
    }

    return rt_string_from_bytes(buffer, len);
}

/// @brief Converts a timestamp to ISO 8601 format (UTC).
///
/// Formats a Unix timestamp as an ISO 8601 date/time string in UTC. This
/// format is widely used for data interchange, APIs, and logging because
/// it's unambiguous and machine-parseable.
///
/// **Format:** `YYYY-MM-DDTHH:MM:SSZ`
///
/// Where:
/// - `YYYY`: 4-digit year
/// - `MM`: 2-digit month (01-12)
/// - `DD`: 2-digit day (01-31)
/// - `T`: Literal separator between date and time
/// - `HH`: 2-digit hour in 24h format (00-23)
/// - `MM`: 2-digit minute (00-59)
/// - `SS`: 2-digit second (00-59)
/// - `Z`: Indicates UTC (Zulu time)
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
/// Dim iso = DateTime.ToISO(now)
/// Print iso  ' "2023-12-19T14:30:45Z"
///
/// ' Use for API calls
/// Dim json = "{""timestamp"": """ & iso & """}"
/// ```
///
/// @param timestamp Unix timestamp (seconds since epoch).
///
/// @return ISO 8601 formatted string in UTC, or empty string if invalid.
///
/// @note Always uses UTC (not local time) - the 'Z' suffix indicates this.
/// @note Does not include milliseconds (only second precision).
/// @note O(1) time complexity.
///
/// @see rt_datetime_format For custom formatting in local time
/// @see rt_datetime_create For creating timestamps from components
rt_string rt_datetime_to_iso(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_gmtime_r(&t, &tm_buf); // Use UTC for ISO format
    if (!tm)
    {
        return rt_string_from_bytes("", 0);
    }

    char buffer[32];
    int len = snprintf(buffer,
                       sizeof(buffer),
                       "%04d-%02d-%02dT%02d:%02d:%02dZ",
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_hour,
                       tm->tm_min,
                       tm->tm_sec);

    return rt_string_from_bytes(buffer, (size_t)len);
}

/// @brief Converts a timestamp to local ISO 8601 format (no Z suffix).
///
/// Like rt_datetime_to_iso but uses local time instead of UTC.
/// Format: YYYY-MM-DDTHH:MM:SS
///
/// @param timestamp Unix timestamp (seconds since epoch).
/// @return Local ISO 8601 formatted string, or empty string if invalid.
rt_string rt_datetime_to_local(int64_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&t, &tm_buf);
    if (!tm)
    {
        return rt_string_from_bytes("", 0);
    }

    char buffer[32];
    int len = snprintf(buffer,
                       sizeof(buffer),
                       "%04d-%02d-%02dT%02d:%02d:%02d",
                       tm->tm_year + 1900,
                       tm->tm_mon + 1,
                       tm->tm_mday,
                       tm->tm_hour,
                       tm->tm_min,
                       tm->tm_sec);

    return rt_string_from_bytes(buffer, (size_t)len);
}

/// @brief Creates a Unix timestamp from date/time components.
///
/// Combines year, month, day, hour, minute, and second components into a
/// Unix timestamp. The components are interpreted in the local time zone.
///
/// **Parameter ranges:**
/// | Parameter | Range       | Notes                    |
/// |-----------|-------------|--------------------------|
/// | year      | 1970-2038+  | Full 4-digit year        |
/// | month     | 1-12        | January = 1              |
/// | day       | 1-31        | Depends on month         |
/// | hour      | 0-23        | 24-hour format           |
/// | minute    | 0-59        |                          |
/// | second    | 0-59        |                          |
///
/// **Example:**
/// ```
/// ' Create timestamp for Christmas 2023 at noon
/// Dim christmas = DateTime.Create(2023, 12, 25, 12, 0, 0)
/// Print DateTime.ToISO(christmas)  ' "2023-12-25T..."
///
/// ' Calculate days until an event
/// Dim now = DateTime.Now()
/// Dim diff = DateTime.Diff(christmas, now)
/// Print "Days until Christmas: " & (diff / 86400)
/// ```
///
/// **Overflow handling:**
/// The function normalizes overflow values. For example:
/// - Month 13 → January of next year
/// - Day 32 of January → February 1
/// - Hour 25 → 1 AM next day
///
/// @param year The year (e.g., 2023).
/// @param month The month (1-12, where 1 = January).
/// @param day The day of month (1-31).
/// @param hour The hour in 24-hour format (0-23).
/// @param minute The minute (0-59).
/// @param second The second (0-59).
///
/// @return Unix timestamp representing the specified date/time, or -1 if
///         the date cannot be represented.
///
/// @note Uses local time zone for interpretation.
/// @note Automatically handles daylight saving time transitions.
/// @note O(1) time complexity.
///
/// @see rt_datetime_year For extracting components from a timestamp
/// @see rt_datetime_to_iso For formatting timestamps
int64_t rt_datetime_create(
    int64_t year, int64_t month, int64_t day, int64_t hour, int64_t minute, int64_t second)
{
    struct tm tm = {0};
    tm.tm_year = (int)(year - 1900);
    tm.tm_mon = (int)(month - 1);
    tm.tm_mday = (int)day;
    tm.tm_hour = (int)hour;
    tm.tm_min = (int)minute;
    tm.tm_sec = (int)second;
    tm.tm_isdst = -1; // Let the system determine DST

    time_t t = mktime(&tm);
    return (int64_t)t;
}

/// @brief Adds seconds to a timestamp.
///
/// Returns a new timestamp that is the specified number of seconds later
/// (or earlier, if seconds is negative) than the input timestamp.
///
/// **Example:**
/// ```
/// Dim now = DateTime.Now()
///
/// ' 1 hour later
/// Dim later = DateTime.AddSeconds(now, 3600)
///
/// ' 30 minutes earlier
/// Dim earlier = DateTime.AddSeconds(now, -1800)
///
/// ' 1 week later
/// Dim nextWeek = DateTime.AddSeconds(now, 7 * 24 * 60 * 60)
/// ```
///
/// **Common time intervals in seconds:**
/// | Duration  | Seconds    |
/// |-----------|------------|
/// | 1 minute  | 60         |
/// | 1 hour    | 3,600      |
/// | 1 day     | 86,400     |
/// | 1 week    | 604,800    |
///
/// @param timestamp Unix timestamp to add to.
/// @param seconds Number of seconds to add (can be negative).
///
/// @return New timestamp offset by the specified seconds.
///
/// @note O(1) time complexity - simple addition.
/// @note No overflow checking is performed.
///
/// @see rt_datetime_add_days For adding days
/// @see rt_datetime_diff For calculating time differences
int64_t rt_datetime_add_seconds(int64_t timestamp, int64_t seconds)
{
    return timestamp + seconds;
}

/// @brief Adds days to a timestamp.
///
/// Returns a new timestamp that is the specified number of days later
/// (or earlier, if days is negative) than the input timestamp. One day
/// is exactly 86,400 seconds.
///
/// **Example:**
/// ```
/// Dim today = DateTime.Now()
///
/// ' Tomorrow
/// Dim tomorrow = DateTime.AddDays(today, 1)
///
/// ' Yesterday
/// Dim yesterday = DateTime.AddDays(today, -1)
///
/// ' One month from now (approximately)
/// Dim nextMonth = DateTime.AddDays(today, 30)
/// ```
///
/// @param timestamp Unix timestamp to add to.
/// @param days Number of days to add (can be negative).
///
/// @return New timestamp offset by the specified days.
///
/// @note O(1) time complexity - simple multiplication and addition.
/// @note Does not account for daylight saving time transitions (always adds
///       exactly 86,400 seconds per day). For calendar-aware day arithmetic,
///       use DateTime.Create with adjusted day values.
/// @note No overflow checking is performed.
///
/// @see rt_datetime_add_seconds For adding arbitrary time intervals
/// @see rt_datetime_diff For calculating time differences
int64_t rt_datetime_add_days(int64_t timestamp, int64_t days)
{
    return timestamp + (days * 86400); // 86400 seconds per day
}

/// @brief Calculates the difference between two timestamps in seconds.
///
/// Returns the number of seconds between two timestamps (ts1 - ts2). A positive
/// result means ts1 is later than ts2; negative means ts1 is earlier.
///
///
/// **Example:**
/// ```
/// Dim start = DateTime.Now()
/// ' ... do some work ...
/// Dim finish = DateTime.Now()
///
/// Dim elapsed = DateTime.Diff(finish, start)
/// Print "Operation took " & elapsed & " seconds"
///
/// ' Convert to other units
/// Dim minutes = elapsed / 60
/// Dim hours = elapsed / 3600
/// Dim days = elapsed / 86400
/// ```
///
/// **Age calculation example:**
/// ```
/// Dim birthdate = DateTime.Create(1990, 6, 15, 0, 0, 0)
/// Dim now = DateTime.Now()
/// Dim ageSeconds = DateTime.Diff(now, birthdate)
/// Dim ageYears = ageSeconds / (365.25 * 86400)
/// Print "Age: " & Int(ageYears) & " years"
/// ```
///
/// @param ts1 First timestamp (minuend).
/// @param ts2 Second timestamp (subtrahend).
///
/// @return Difference in seconds (ts1 - ts2). Positive if ts1 > ts2,
///         negative if ts1 < ts2, zero if equal.
///
/// @note O(1) time complexity - simple subtraction.
/// @note The order of parameters matters: Diff(later, earlier) gives positive.
///
/// @see rt_datetime_add_seconds For the inverse operation
/// @see rt_datetime_add_days For adding day intervals
int64_t rt_datetime_diff(int64_t ts1, int64_t ts2)
{
    return ts1 - ts2;
}

//=============================================================================
// Parsing Functions
//=============================================================================

/// @brief Helper to check if a character is a digit.
static int dt_is_digit(char c) { return c >= '0' && c <= '9'; }

/// @brief Helper to parse exactly N digits from a string.
/// @return The parsed integer, or -1 if insufficient digits.
static int dt_parse_digits(const char *s, int n, const char **end)
{
    int val = 0;
    for (int i = 0; i < n; ++i)
    {
        if (!dt_is_digit(s[i]))
            return -1;
        val = val * 10 + (s[i] - '0');
    }
    *end = s + n;
    return val;
}

int64_t rt_datetime_parse_iso(rt_string s)
{
    const char *str = rt_string_cstr(s);
    if (!str)
        return 0;

    const char *p = str;
    const char *end;

    // Parse YYYY-MM-DDTHH:MM:SS[Z]
    int year = dt_parse_digits(p, 4, &end);
    if (year < 0 || *end != '-')
        return 0;
    p = end + 1;

    int month = dt_parse_digits(p, 2, &end);
    if (month < 0 || *end != '-')
        return 0;
    p = end + 1;

    int day = dt_parse_digits(p, 2, &end);
    if (day < 0)
        return 0;
    p = end;

    if (*p != 'T' && *p != 't' && *p != ' ')
        return 0;
    p++;

    int hour = dt_parse_digits(p, 2, &end);
    if (hour < 0 || *end != ':')
        return 0;
    p = end + 1;

    int minute = dt_parse_digits(p, 2, &end);
    if (minute < 0 || *end != ':')
        return 0;
    p = end + 1;

    int second = dt_parse_digits(p, 2, &end);
    if (second < 0)
        return 0;
    p = end;

    // Check for Z suffix (UTC) or end of string
    int is_utc = (*p == 'Z' || *p == 'z');

    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;

    if (is_utc)
    {
        // Portable UTC mktime: use mktime (local), then adjust by the UTC offset.
        // First, convert with mktime (local time interpretation)
        struct tm utc_tm = tm;
        utc_tm.tm_isdst = 0;
        time_t local_t = mktime(&utc_tm);
        if (local_t == (time_t)-1)
            return 0;
        // Find the UTC offset: gmtime(local_t) gives UTC representation
        struct tm gm_buf;
        struct tm *gm = rt_gmtime_r(&local_t, &gm_buf);
        if (!gm)
            return 0;
        // Difference between local interpretation and actual UTC
        struct tm local_buf;
        struct tm *loc = rt_localtime_r(&local_t, &local_buf);
        if (!loc)
            return 0;
        // UTC offset in seconds
        int64_t utc_off = (int64_t)mktime(loc) - (int64_t)mktime(gm);
        return (int64_t)local_t - utc_off;
    }
    else
    {
        tm.tm_isdst = -1;
        time_t t = mktime(&tm);
        return (int64_t)t;
    }
}

int64_t rt_datetime_parse_date(rt_string s)
{
    const char *str = rt_string_cstr(s);
    if (!str)
        return 0;

    const char *p = str;
    const char *end;

    // Parse YYYY-MM-DD
    int year = dt_parse_digits(p, 4, &end);
    if (year < 0 || *end != '-')
        return 0;
    p = end + 1;

    int month = dt_parse_digits(p, 2, &end);
    if (month < 0 || *end != '-')
        return 0;
    p = end + 1;

    int day = dt_parse_digits(p, 2, &end);
    if (day < 0)
        return 0;

    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;

    time_t t = mktime(&tm);
    return (int64_t)t;
}

int64_t rt_datetime_parse_time(rt_string s)
{
    const char *str = rt_string_cstr(s);
    if (!str)
        return -1;

    const char *p = str;
    const char *end;

    // Parse HH:MM[:SS]
    int hour = dt_parse_digits(p, 2, &end);
    if (hour < 0 || *end != ':')
        return -1;
    p = end + 1;

    int minute = dt_parse_digits(p, 2, &end);
    if (minute < 0)
        return -1;
    p = end;

    int second = 0;
    if (*p == ':')
    {
        p++;
        second = dt_parse_digits(p, 2, &end);
        if (second < 0)
            return -1;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return -1;

    return (int64_t)(hour * 3600 + minute * 60 + second);
}

int64_t rt_datetime_try_parse(rt_string s)
{
    const char *str = rt_string_cstr(s);
    if (!str || *str == '\0')
        return 0;

    size_t len = strlen(str);

    // Try ISO 8601 first (contains 'T' or space separator)
    if (len >= 19)
    {
        int64_t result = rt_datetime_parse_iso(s);
        if (result != 0)
            return result;
    }

    // Try date-only (YYYY-MM-DD, length 10)
    if (len == 10 && str[4] == '-' && str[7] == '-')
    {
        int64_t result = rt_datetime_parse_date(s);
        if (result != 0)
            return result;
    }

    // Try time-only (HH:MM or HH:MM:SS)
    if ((len == 5 || len == 8) && str[2] == ':')
    {
        int64_t result = rt_datetime_parse_time(s);
        if (result >= 0)
            return result;
    }

    return 0;
}
