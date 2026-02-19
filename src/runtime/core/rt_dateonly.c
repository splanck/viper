//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_dateonly.c
// Purpose: DateOnly type implementation.
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

typedef struct
{
    int64_t year;
    int64_t month;
    int64_t day;
} DateOnly;

//=============================================================================
// Helper Functions
//=============================================================================

static int8_t is_leap_year(int64_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int64_t days_in_month_impl(int64_t year, int64_t month)
{
    static const int64_t days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && is_leap_year(year))
        return 29;
    return days[month];
}

// Convert date to days since epoch (Jan 1, 1970)
static int64_t to_days_since_epoch(int64_t year, int64_t month, int64_t day)
{
    // Adjust for months starting from March
    int64_t a = (14 - month) / 12;
    int64_t y = year + 4800 - a;
    int64_t m = month + 12 * a - 3;

    // Julian day number
    int64_t jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;

    // Subtract Unix epoch (Jan 1, 1970 = JDN 2440588)
    return jdn - 2440588;
}

// Convert days since epoch to date components
static void from_days_since_epoch(int64_t days, int64_t *year, int64_t *month, int64_t *day)
{
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

void *rt_dateonly_create(int64_t year, int64_t month, int64_t day)
{
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

void *rt_dateonly_today(void)
{
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm = rt_localtime_r(&now, &tm_buf);
    if (!tm)
        return NULL;
    return rt_dateonly_create(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

void *rt_dateonly_parse(rt_string s)
{
    const char *str = rt_string_cstr(s);
    int year, month, day;

    if (sscanf(str, "%d-%d-%d", &year, &month, &day) != 3)
        return NULL;

    return rt_dateonly_create(year, month, day);
}

void *rt_dateonly_from_days(int64_t days)
{
    int64_t year, month, day;
    from_days_since_epoch(days, &year, &month, &day);
    return rt_dateonly_create(year, month, day);
}

//=============================================================================
// Component Access
//=============================================================================

int64_t rt_dateonly_year(void *obj)
{
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return d->year;
}

int64_t rt_dateonly_month(void *obj)
{
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return d->month;
}

int64_t rt_dateonly_day(void *obj)
{
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return d->day;
}

int64_t rt_dateonly_day_of_week(void *obj)
{
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;

    // Zeller's formula (modified for Sunday = 0)
    int64_t days = to_days_since_epoch(d->year, d->month, d->day);
    // Jan 1, 1970 was Thursday (day 4)
    return (days + 4) % 7;
}

int64_t rt_dateonly_day_of_year(void *obj)
{
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;

    int64_t doy = 0;
    for (int64_t m = 1; m < d->month; m++)
    {
        doy += days_in_month_impl(d->year, m);
    }
    doy += d->day;
    return doy;
}

int64_t rt_dateonly_to_days(void *obj)
{
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return to_days_since_epoch(d->year, d->month, d->day);
}

//=============================================================================
// Date Arithmetic
//=============================================================================

void *rt_dateonly_add_days(void *obj, int64_t days)
{
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    int64_t total = to_days_since_epoch(d->year, d->month, d->day) + days;
    return rt_dateonly_from_days(total);
}

void *rt_dateonly_add_months(void *obj, int64_t months)
{
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;

    int64_t year = d->year;
    int64_t month = d->month + months;

    // Normalize months
    while (month > 12)
    {
        month -= 12;
        year++;
    }
    while (month < 1)
    {
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

void *rt_dateonly_add_years(void *obj, int64_t years)
{
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;

    int64_t year = d->year + years;
    int64_t month = d->month;
    int64_t day = d->day;

    // Handle Feb 29 in non-leap years
    if (month == 2 && day == 29 && !is_leap_year(year))
    {
        day = 28;
    }

    return rt_dateonly_create(year, month, day);
}

int64_t rt_dateonly_diff_days(void *a, void *b)
{
    if (!a || !b)
        return 0;
    return rt_dateonly_to_days(a) - rt_dateonly_to_days(b);
}

//=============================================================================
// Date Queries
//=============================================================================

int8_t rt_dateonly_is_leap_year(void *obj)
{
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return is_leap_year(d->year);
}

int64_t rt_dateonly_days_in_month(void *obj)
{
    if (!obj)
        return 0;
    DateOnly *d = (DateOnly *)obj;
    return days_in_month_impl(d->year, d->month);
}

void *rt_dateonly_start_of_month(void *obj)
{
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    return rt_dateonly_create(d->year, d->month, 1);
}

void *rt_dateonly_end_of_month(void *obj)
{
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    return rt_dateonly_create(d->year, d->month, days_in_month_impl(d->year, d->month));
}

void *rt_dateonly_start_of_year(void *obj)
{
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    return rt_dateonly_create(d->year, 1, 1);
}

void *rt_dateonly_end_of_year(void *obj)
{
    if (!obj)
        return NULL;
    DateOnly *d = (DateOnly *)obj;
    return rt_dateonly_create(d->year, 12, 31);
}

//=============================================================================
// Comparison
//=============================================================================

int64_t rt_dateonly_cmp(void *a, void *b)
{
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

int8_t rt_dateonly_equals(void *a, void *b)
{
    return rt_dateonly_cmp(a, b) == 0 ? 1 : 0;
}

//=============================================================================
// Formatting
//=============================================================================

rt_string rt_dateonly_to_string(void *obj)
{
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
    return rt_string_from_bytes(buf, (int64_t)strlen(buf));
}

rt_string rt_dateonly_format(void *obj, rt_string fmt)
{
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

    for (int64_t i = 0; i < fmt_len && buf_pos < 255; i++)
    {
        if (fmt_str[i] == '%' && i + 1 < fmt_len)
        {
            i++;
            char spec = fmt_str[i];
            switch (spec)
            {
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
        }
        else
        {
            buf[buf_pos++] = fmt_str[i];
        }
    }

    buf[buf_pos] = '\0';
    return rt_string_from_bytes(buf, buf_pos);
}
