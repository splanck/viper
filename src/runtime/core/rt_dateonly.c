//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_dateonly.c
// Purpose: Implements the DateOnly type for the Viper runtime, representing a
//          calendar date (year, month, day) without a time component. Provides
//          construction from components or Unix day offsets, arithmetic
//          (AddDays, DiffDays), comparison, formatting, and leap-year handling.
//
// Key invariants:
//   - Dates are represented internally as (year, month, day) tuples with no
//     timezone or time information.
//   - Internal conversion between dates and "days since Unix epoch" uses the
//     Julian Day Number algorithm for correctness across the Gregorian calendar.
//   - Month values are 1-based (January = 1, December = 12).
//   - Leap years follow the Gregorian rule: divisible by 4, except centuries
//     unless also divisible by 400.
//   - Out-of-range month/day inputs produce 0 from days_in_month_impl rather
//     than trapping; validation responsibility lies with the caller.
//
// Ownership/Lifetime:
//   - DateOnly instances are heap-allocated via rt_obj_new_i64 and managed by
//     the runtime GC; callers do not free them explicitly.
//   - Formatted strings are newly allocated rt_string values; the caller owns
//     the reference and must call rt_string_unref when done.
//
// Links: src/runtime/core/rt_dateonly.h (public API),
//        src/runtime/core/rt_datetime.c (full DateTime with time components),
//        src/runtime/core/rt_daterange.c (DateRange spanning two timestamps)
//
//===----------------------------------------------------------------------===//

#include "rt_dateonly.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct {
    int64_t year;
    int64_t month;
    int64_t day;
} DateOnly;

//=============================================================================
// Helper Functions
//=============================================================================

static int8_t is_leap_year(int64_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int64_t days_in_month_impl(int64_t year, int64_t month) {
    static const int64_t days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && is_leap_year(year))
        return 29;
    return days[month];
}

/// @brief Convert a Gregorian date to days since Unix epoch (1970-01-01).
/// @details Uses the Julian Day Number algorithm: first adjusts the calendar
///          so March is month 0 (simplifies the variable-length February),
///          computes the JDN, then subtracts the JDN of the Unix epoch
///          (2440588). The algorithm is valid for any proleptic Gregorian date.
static int64_t to_days_since_epoch(int64_t year, int64_t month, int64_t day) {
    // Adjust for months starting from March
    int64_t a = (14 - month) / 12;
    int64_t y = year + 4800 - a;
    int64_t m = month + 12 * a - 3;

    // Julian day number
    int64_t jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;

    // Subtract Unix epoch (Jan 1, 1970 = JDN 2440588)
    return jdn - 2440588;
}

/// @brief Convert days since Unix epoch back to Gregorian year/month/day.
/// @details Inverse of to_days_since_epoch. Adds the epoch JDN, then reverses
///          the Julian Day Number formula to recover year, month, and day.
///          The magic constants (146097, 1461, 153) come from the cycle lengths
///          of the Gregorian calendar: 400-year cycle = 146097 days,
///          4-year cycle = 1461 days, 5-month group = 153 days.
static void from_days_since_epoch(int64_t days, int64_t *year, int64_t *month, int64_t *day) {
    // Add Unix epoch offset
    int64_t jdn = days + 2440588;

    // Convert from Julian day number
    int64_t a = jdn + 32044;
    int64_t b = (4 * a + 3) / 146097;
    int64_t c = a - (146097 * b) / 4;
    int64_t d = (4 * c + 3) / 1461;
    int64_t e = c - (1461 * d) / 4;
    int64_t m = (5 * e + 2) / 153;

    *day = e - (153 * m + 2) / 5 + 1;
    *month = m + 3 - 12 * (m / 10);
    *year = 100 * b + d - 4800 + m / 10;
}

//=============================================================================
// DateOnly Creation
//=============================================================================

/// @brief Create a DateOnly from explicit year, month, and day components.
/// @details Validates that month is in [1,12] and day is in [1, days-in-month].
///          Returns NULL for invalid inputs rather than trapping, allowing callers
///          to provide their own error handling.
/// @param year Gregorian year (e.g. 2026).
/// @param month Month number (1=January, 12=December).
/// @param day Day of month (1-based).
/// @return New GC-managed DateOnly, or NULL if inputs are out of range.
void *rt_dateonly_create(int64_t year, int64_t month, int64_t day) {
    // Validate inputs
    if (month < 1 || month > 12)
        return NULL;
    int64_t max_day = days_in_month_impl(year, month);
    if (day < 1 || day > max_day)
        return NULL;

    DateOnly *d = (DateOnly *)rt_obj_new_i64(0, (int64_t)sizeof(DateOnly));

    d->year = year;
    d->month = month;
    d->day = day;
    return d;
}

/// @brief Return a DateOnly representing today's date in local time.
/// @details Uses the platform's localtime_r to convert the current Unix
///          timestamp to a calendar date. The result reflects the system's
///          local timezone setting (not UTC).
/// @return New DateOnly for today, or NULL if the system clock fails.
void *rt_dateonly_today(void) {
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&now, &tm_buf);
    if (!tm)
        return NULL;
    return rt_dateonly_create(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

/// @brief Parse a DateOnly from an ISO 8601 date string (YYYY-MM-DD).
/// @details Uses sscanf to extract three integers separated by hyphens. Rejects
///          strings that don't match the expected format. Delegates validation
///          of month/day ranges to rt_dateonly_create.
/// @param s Runtime string containing the date text.
/// @return New DateOnly, or NULL if the string is malformed or out of range.
void *rt_dateonly_parse(rt_string s) {
    const char *str = rt_string_cstr(s);
    int year, month, day;

    if (sscanf(str, "%d-%d-%d", &year, &month, &day) != 3)
        return NULL;

    return rt_dateonly_create(year, month, day);
}

/// @brief Create a DateOnly from a days-since-epoch count.
/// @details Converts the signed day offset back to year/month/day using the
///          inverse Julian Day Number algorithm. Day 0 = January 1, 1970.
///          Negative values represent dates before the Unix epoch.
/// @param days Signed offset from 1970-01-01.
/// @return New DateOnly for the corresponding calendar date.
void *rt_dateonly_from_days(int64_t days) {
    int64_t year, month, day;
    from_days_since_epoch(days, &year, &month, &day);
    return rt_dateonly_create(year, month, day);
}

//=============================================================================
// Component Access
//=============================================================================

/// @brief Return the year component of a DateOnly (e.g. 2026).
/// @param obj DateOnly object pointer; returns 0 if NULL.
/// @return Four-digit year.
int64_t rt_dateonly_year(void *obj) {
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return d->year;
}

/// @brief Return the month component (1=January, 12=December).
/// @param obj DateOnly object pointer; returns 0 if NULL.
/// @return Month number in the range 1-12.
int64_t rt_dateonly_month(void *obj) {
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return d->month;
}

/// @brief Return the day-of-month component (1-31).
/// @param obj DateOnly object pointer; returns 0 if NULL.
/// @return Day number in the range 1-31.
int64_t rt_dateonly_day(void *obj) {
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return d->day;
}

/// @brief Return the day of week (0=Sunday through 6=Saturday).
/// @details Converts the date to days-since-epoch, then offsets by 4 because
///          January 1, 1970 was a Thursday (day index 4 in a Sunday-start week).
///          The modulo 7 produces the correct weekday for any date.
/// @param obj DateOnly object pointer; returns 0 if NULL.
/// @return Day-of-week index: 0=Sunday, 1=Monday, ..., 6=Saturday.
int64_t rt_dateonly_day_of_week(void *obj) {
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;

    // Zeller's formula (modified for Sunday = 0)
    int64_t days = to_days_since_epoch(d->year, d->month, d->day);
    // Jan 1, 1970 was Thursday (day 4)
    return (days + 4) % 7;
}

/// @brief Return the 1-based day-of-year (1-366).
/// @details Sums the number of days in all preceding months (accounting for
///          leap years in February) then adds the current day. January 1 = 1.
/// @param obj DateOnly object pointer; returns 0 if NULL.
/// @return Day-of-year in the range 1-366.
int64_t rt_dateonly_day_of_year(void *obj) {
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;

    int64_t doy = 0;
    for (int64_t m = 1; m < d->month; m++) {
        doy += days_in_month_impl(d->year, m);
    }
    doy += d->day;
    return doy;
}

/// @brief Convert the date to days since Unix epoch (1970-01-01 = day 0).
/// @details Delegates to the Julian Day Number conversion. Useful for
///          serialization, date arithmetic, and comparing dates numerically.
/// @param obj DateOnly object pointer; returns 0 if NULL.
/// @return Signed day offset (negative for dates before 1970).
int64_t rt_dateonly_to_days(void *obj) {
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return to_days_since_epoch(d->year, d->month, d->day);
}

//=============================================================================
// Date Arithmetic
//=============================================================================

/// @brief Return a new DateOnly shifted by the given number of days.
/// @details Converts to days-since-epoch, adds the offset, and converts back.
///          Handles month/year boundary crossings automatically via the epoch
///          round-trip. Negative values move backward in time.
/// @param obj Source DateOnly (not modified).
/// @param days Signed number of days to add.
/// @return New DateOnly for the resulting calendar date.
void *rt_dateonly_add_days(void *obj, int64_t days) {
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    int64_t total = to_days_since_epoch(d->year, d->month, d->day) + days;
    return rt_dateonly_from_days(total);
}

/// @brief Return a new DateOnly shifted by the given number of months.
/// @details Adds the month offset then normalizes year/month. If the resulting
///          day exceeds the new month's length (e.g. Jan 31 + 1 month → Feb 28),
///          it clamps to the last day. Special case: Feb 29 → Feb 28 when the
///          target year is not a leap year.
/// @param obj Source DateOnly (not modified).
/// @param months Signed number of months to add (negative = subtract).
/// @return New DateOnly with clamped day-of-month.
void *rt_dateonly_add_months(void *obj, int64_t months) {
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;

    int64_t year = d->year;
    int64_t month = d->month + months;

    // Normalize months
    while (month > 12) {
        month -= 12;
        year++;
    }
    while (month < 1) {
        month += 12;
        year--;
    }

    // Clamp day to valid range for new month
    int64_t max_day = days_in_month_impl(year, month);
    int64_t day = d->day;
    if (day > max_day)
        day = max_day;

    return rt_dateonly_create(year, month, day);
}

/// @brief Return a new DateOnly shifted by the given number of years.
/// @details Delegates to add_months(obj, years * 12). This handles the Feb 29
///          leap-year edge case correctly — adding 1 year to Feb 29 gives Feb 28
///          in a non-leap year.
/// @param obj Source DateOnly (not modified).
/// @param years Signed number of years to add.
/// @return New DateOnly.
void *rt_dateonly_add_years(void *obj, int64_t years) {
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;

    int64_t year = d->year + years;
    int64_t month = d->month;
    int64_t day = d->day;

    // Handle Feb 29 in non-leap years
    if (month == 2 && day == 29 && !is_leap_year(year)) {
        day = 28;
    }

    return rt_dateonly_create(year, month, day);
}

/// @brief Return the signed difference in days between two dates (a - b).
/// @details Converts both dates to days-since-epoch and subtracts. Positive
///          result means a is later than b; negative means a is earlier.
/// @param a First DateOnly (the "later" date in positive-result convention).
/// @param b Second DateOnly (subtracted from a).
/// @return Signed day difference; 0 if either input is NULL.
int64_t rt_dateonly_diff_days(void *a, void *b) {
    if (!a || !b)
        return 0;
    return rt_dateonly_to_days(a) - rt_dateonly_to_days(b);
}

//=============================================================================
// Date Queries
//=============================================================================

/// @brief Check if the date's year is a Gregorian leap year.
/// @details Leap year rule: divisible by 4, except centuries unless also
///          divisible by 400. So 2000 is a leap year but 1900 is not.
/// @param obj DateOnly object pointer; returns 0 if NULL.
/// @return 1 if the year is a leap year, 0 otherwise.
int8_t rt_dateonly_is_leap_year(void *obj) {
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return is_leap_year(d->year);
}

/// @brief Return the number of days in the date's month (28-31).
/// @details Accounts for leap years when the month is February.
/// @param obj DateOnly object pointer; returns 0 if NULL.
/// @return Day count: 28 or 29 for Feb, 30 or 31 for other months.
int64_t rt_dateonly_days_in_month(void *obj) {
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return days_in_month_impl(d->year, d->month);
}

/// @brief Return a new DateOnly for the first day of the same month.
void *rt_dateonly_start_of_month(void *obj) {
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    return rt_dateonly_create(d->year, d->month, 1);
}

/// @brief Return a new DateOnly for the last day of the same month.
void *rt_dateonly_end_of_month(void *obj) {
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    return rt_dateonly_create(d->year, d->month, days_in_month_impl(d->year, d->month));
}

/// @brief Return a new DateOnly for January 1 of the same year.
void *rt_dateonly_start_of_year(void *obj) {
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    return rt_dateonly_create(d->year, 1, 1);
}

/// @brief Return a new DateOnly for December 31 of the same year.
void *rt_dateonly_end_of_year(void *obj) {
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    return rt_dateonly_create(d->year, 12, 31);
}

//=============================================================================
// Comparison
//=============================================================================

/// @brief Compare two dates chronologically.
/// @details Converts both to days-since-epoch for a single integer comparison.
///          NULL is treated as "less than" any valid date, so NULL < any date.
/// @param a First DateOnly.
/// @param b Second DateOnly.
/// @return -1 if a is earlier, 0 if equal, 1 if a is later.
int64_t rt_dateonly_cmp(void *a, void *b) {
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;

    int64_t days_a = rt_dateonly_to_days(a);
    int64_t days_b = rt_dateonly_to_days(b);

    if (days_a < days_b)
        return -1;
    if (days_a > days_b)
        return 1;
    return 0;
}

/// @brief Check if two dates represent the same calendar day.
/// @param a First DateOnly.
/// @param b Second DateOnly.
/// @return 1 if both represent the same date, 0 otherwise.
int8_t rt_dateonly_equals(void *a, void *b) {
    return rt_dateonly_cmp(a, b) == 0 ? 1 : 0;
}

//=============================================================================
// Formatting
//=============================================================================

/// @brief Format the date as an ISO 8601 string (YYYY-MM-DD).
/// @details Uses zero-padded fields so the output is always 10 characters
///          (e.g. "2026-03-29"). This is the canonical serialization format
///          and is accepted by rt_dateonly_parse for round-tripping.
/// @param obj DateOnly object pointer; returns "" if NULL.
/// @return Newly allocated runtime string in ISO 8601 format.
rt_string rt_dateonly_to_string(void *obj) {
    if (!obj)
        return rt_const_cstr("");

    DateOnly *d = (DateOnly *)obj;
    char buf[32];
    snprintf(buf,
             sizeof(buf),
             "%04lld-%02lld-%02lld",
             (long long)d->year,
             (long long)d->month,
             (long long)d->day);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format the date using a custom format string.
/// @details Supports strftime-style specifiers: %Y (4-digit year), %m (2-digit
///          month), %d (2-digit day), %B (full month name), %b (abbreviated
///          month name), %A (full weekday name), %a (abbreviated weekday name).
///          Literal percent signs are written with %%. Characters that don't
///          follow a % are copied verbatim.
/// @param obj DateOnly object pointer; returns "" if NULL.
/// @param fmt Format string containing specifiers.
/// @return Newly allocated runtime string with the formatted result.
rt_string rt_dateonly_format(void *obj, rt_string fmt) {
    if (!obj)
        return rt_const_cstr("");

    DateOnly *d = (DateOnly *)obj;
    const char *fmt_str = rt_string_cstr(fmt);
    int64_t fmt_len = rt_str_len(fmt);

    static const char *month_names[] = {"",
                                        "January",
                                        "February",
                                        "March",
                                        "April",
                                        "May",
                                        "June",
                                        "July",
                                        "August",
                                        "September",
                                        "October",
                                        "November",
                                        "December"};
    static const char *month_abbr[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *day_names[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *day_abbr[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    char buf[256];
    int64_t buf_pos = 0;

    for (int64_t i = 0; i < fmt_len && buf_pos < 255; i++) {
        if (fmt_str[i] == '%' && i + 1 < fmt_len) {
            i++;
            char spec = fmt_str[i];
            switch (spec) {
                case 'Y': // 4-digit year
                    buf_pos += snprintf(
                        buf + buf_pos, 256 - (size_t)buf_pos, "%04lld", (long long)d->year);
                    break;
                case 'y': // 2-digit year
                    buf_pos += snprintf(
                        buf + buf_pos, 256 - (size_t)buf_pos, "%02lld", (long long)(d->year % 100));
                    break;
                case 'm': // 2-digit month
                    buf_pos += snprintf(
                        buf + buf_pos, 256 - (size_t)buf_pos, "%02lld", (long long)d->month);
                    break;
                case 'd': // 2-digit day
                    buf_pos +=
                        snprintf(buf + buf_pos, 256 - (size_t)buf_pos, "%02lld", (long long)d->day);
                    break;
                case 'B': // Full month name
                    if (d->month >= 1 && d->month <= 12)
                        buf_pos += snprintf(
                            buf + buf_pos, 256 - (size_t)buf_pos, "%s", month_names[d->month]);
                    break;
                case 'b': // Abbreviated month name
                    if (d->month >= 1 && d->month <= 12)
                        buf_pos += snprintf(
                            buf + buf_pos, 256 - (size_t)buf_pos, "%s", month_abbr[d->month]);
                    break;
                case 'A': // Full day name
                {
                    int64_t dow = rt_dateonly_day_of_week(obj);
                    if (dow >= 0 && dow <= 6)
                        buf_pos +=
                            snprintf(buf + buf_pos, 256 - (size_t)buf_pos, "%s", day_names[dow]);
                    break;
                }
                case 'a': // Abbreviated day name
                {
                    int64_t dow = rt_dateonly_day_of_week(obj);
                    if (dow >= 0 && dow <= 6)
                        buf_pos +=
                            snprintf(buf + buf_pos, 256 - (size_t)buf_pos, "%s", day_abbr[dow]);
                    break;
                }
                case 'j': // Day of year
                    buf_pos += snprintf(buf + buf_pos,
                                        256 - (size_t)buf_pos,
                                        "%03lld",
                                        (long long)rt_dateonly_day_of_year(obj));
                    break;
                case '%': // Literal %
                    buf[buf_pos++] = '%';
                    break;
                default:
                    buf[buf_pos++] = '%';
                    buf[buf_pos++] = spec;
                    break;
            }
            if (buf_pos > 255)
                buf_pos = 255;
        } else {
            buf[buf_pos++] = fmt_str[i];
        }
    }

    buf[buf_pos] = '\0';
    return rt_string_from_bytes(buf, buf_pos);
}
