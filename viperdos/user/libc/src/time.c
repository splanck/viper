//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/time.c
// Purpose: Time and date functions for ViperDOS libc.
// Key invariants: RTC provides wall-clock; monotonic timer for intervals.
// Ownership/Lifetime: Library; static storage for tm results.
// Links: user/libc/include/time.h
//
//===----------------------------------------------------------------------===//

/**
 * @file time.c
 * @brief Time and date functions for ViperDOS libc.
 *
 * @details
 * This file implements standard C time functions:
 *
 * - Time retrieval: time, clock, gettimeofday, clock_gettime
 * - Time conversion: gmtime, localtime, mktime
 * - Time formatting: strftime
 * - Sleep functions: nanosleep
 *
 * Wall-clock time is provided by the PL031 RTC (SYS_RTC_READ).
 * Monotonic time uses the high-resolution timer (SYS_TIME_NOW_NS).
 */

#include "../include/time.h"
#include "syscall_internal.h"

/* Syscall numbers */
#define SYS_TIME_NOW 0x30
#define SYS_SLEEP 0x31
#define SYS_TIME_NOW_NS 0x34
#define SYS_RTC_READ 0x35

/**
 * @brief Get processor time used.
 *
 * @return Time in milliseconds since boot (CLOCKS_PER_SEC = 1000).
 */
clock_t clock(void) {
    return (clock_t)__syscall1(SYS_TIME_NOW, 0);
}

/**
 * @brief Get current calendar time in seconds.
 *
 * @details
 * Returns the current time as seconds since the Unix epoch
 * (1970-01-01 00:00:00 UTC) if an RTC is available. Falls back
 * to seconds since boot if no RTC is present.
 *
 * @param tloc If non-NULL, receives the time value.
 * @return Current time in seconds since epoch (or since boot).
 */
time_t time(time_t *tloc) {
    /* Try RTC first for real wall-clock time */
    long rtc = __syscall0(SYS_RTC_READ);
    time_t t;
    if (rtc > 0) {
        /* RTC returns Unix timestamp in seconds */
        t = (time_t)rtc;
    } else {
        /* Fallback: uptime in seconds */
        clock_t ms = clock();
        t = ms / 1000;
    }
    if (tloc)
        *tloc = t;
    return t;
}

/**
 * @brief Compute difference between two times.
 *
 * @param time1 Later time value.
 * @param time0 Earlier time value.
 * @return Difference (time1 - time0) in seconds.
 */
long difftime(time_t time1, time_t time0) {
    return (long)(time1 - time0);
}

/**
 * @brief High-resolution sleep.
 *
 * @param req Requested sleep duration.
 * @param rem If non-NULL, receives remaining time on interruption.
 * @return 0 on success, -1 on error.
 */
int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req)
        return -1;

    /* Convert to milliseconds (minimum 1ms if any time requested) */
    long ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    if (ms == 0 && (req->tv_sec > 0 || req->tv_nsec > 0))
        ms = 1;

    __syscall1(SYS_SLEEP, ms);

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}

/**
 * @brief Get time from a specified clock.
 *
 * @details
 * CLOCK_MONOTONIC returns high-resolution nanosecond time since boot.
 * CLOCK_REALTIME returns wall-clock time from the RTC.
 *
 * @param clk_id Clock to query (CLOCK_REALTIME or CLOCK_MONOTONIC).
 * @param tp Structure to receive the time value.
 * @return 0 on success, -1 on error.
 */
int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if (!tp)
        return -1;

    if (clk_id == CLOCK_MONOTONIC) {
        /* High-resolution monotonic time via nanosecond syscall */
        long ns = __syscall0(SYS_TIME_NOW_NS);
        if (ns < 0) {
            /* Fallback to millisecond timer */
            long ms = __syscall1(SYS_TIME_NOW, 0);
            tp->tv_sec = ms / 1000;
            tp->tv_nsec = (ms % 1000) * 1000000L;
        } else {
            tp->tv_sec = ns / 1000000000L;
            tp->tv_nsec = ns % 1000000000L;
        }
        return 0;
    }

    if (clk_id == CLOCK_REALTIME) {
        /* Wall-clock time from RTC */
        long rtc = __syscall0(SYS_RTC_READ);
        if (rtc > 0) {
            tp->tv_sec = rtc;
            /* Sub-second precision from monotonic timer */
            long ms = __syscall1(SYS_TIME_NOW, 0);
            tp->tv_nsec = (ms % 1000) * 1000000L;
        } else {
            /* No RTC, fall back to monotonic */
            long ms = __syscall1(SYS_TIME_NOW, 0);
            tp->tv_sec = ms / 1000;
            tp->tv_nsec = (ms % 1000) * 1000000L;
        }
        return 0;
    }

    return -1;
}

/**
 * @brief Get clock resolution.
 *
 * @param clk_id Clock to query.
 * @param res If non-NULL, receives the clock resolution.
 * @return 0 on success, -1 on invalid clock ID.
 */
int clock_getres(clockid_t clk_id, struct timespec *res) {
    if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
        return -1;

    if (res) {
        if (clk_id == CLOCK_MONOTONIC) {
            /* Nanosecond resolution (16ns on typical QEMU) */
            res->tv_sec = 0;
            res->tv_nsec = 16L;
        } else {
            /* RTC has 1-second resolution */
            res->tv_sec = 1;
            res->tv_nsec = 0;
        }
    }

    return 0;
}

/**
 * @brief Get current time with microsecond precision.
 *
 * @param tv Structure to receive the time value.
 * @param tz Timezone (ignored, pass NULL).
 * @return 0 on success, -1 if tv is NULL.
 */
int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;

    if (!tv)
        return -1;

    /* Try RTC for wall-clock seconds */
    long rtc = __syscall0(SYS_RTC_READ);
    if (rtc > 0) {
        tv->tv_sec = rtc;
        /* Sub-second from monotonic timer */
        long ms = __syscall1(SYS_TIME_NOW, 0);
        tv->tv_usec = (ms % 1000) * 1000L;
    } else {
        /* Fallback to uptime */
        long ms = __syscall1(SYS_TIME_NOW, 0);
        tv->tv_sec = ms / 1000;
        tv->tv_usec = (ms % 1000) * 1000L;
    }

    return 0;
}

/** Static storage for gmtime/localtime results (non-reentrant). */
static struct tm _tm_result;

/* Days per month (non-leap year) */
static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/**
 * @brief Check if a year is a leap year.
 */
static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * @brief Days in a given month (1-indexed month, handles leap years).
 */
static int month_days(int month, int year) {
    if (month == 1 && is_leap_year(year))
        return 29;
    return days_in_month[month];
}

/**
 * @brief Convert time_t to broken-down time (UTC).
 *
 * @details
 * Properly converts Unix timestamps to calendar date/time.
 *
 * @param timep Pointer to time_t value to convert.
 * @return Pointer to static struct tm, or NULL if timep is NULL.
 */
struct tm *gmtime(const time_t *timep) {
    if (!timep)
        return (struct tm *)0;

    time_t t = *timep;

    _tm_result.tm_sec = t % 60;
    t /= 60;
    _tm_result.tm_min = t % 60;
    t /= 60;
    _tm_result.tm_hour = t % 24;
    t /= 24;

    /* t is now days since epoch (1970-01-01, a Thursday) */
    _tm_result.tm_wday = (t + 4) % 7; /* Thursday = day 4 */

    int year = 1970;
    while (1) {
        int days_this_year = is_leap_year(year) ? 366 : 365;
        if (t < days_this_year)
            break;
        t -= days_this_year;
        year++;
    }

    _tm_result.tm_year = year - 1900;
    _tm_result.tm_yday = (int)t;

    int month = 0;
    while (month < 11) {
        int dim = month_days(month, year);
        if (t < dim)
            break;
        t -= dim;
        month++;
    }

    _tm_result.tm_mon = month;
    _tm_result.tm_mday = (int)t + 1;
    _tm_result.tm_isdst = 0;

    return &_tm_result;
}

/**
 * @brief Convert time_t to broken-down local time.
 *
 * @details
 * No timezone support in ViperDOS, equivalent to gmtime().
 *
 * @param timep Pointer to time_t value to convert.
 * @return Pointer to static struct tm, or NULL if timep is NULL.
 */
struct tm *localtime(const time_t *timep) {
    return gmtime(timep);
}

/**
 * @brief Convert broken-down time to time_t.
 *
 * @param tm Pointer to struct tm to convert.
 * @return time_t representation, or -1 if tm is NULL.
 */
time_t mktime(struct tm *tm) {
    if (!tm)
        return -1;

    /* Count days from epoch to the start of tm_year */
    time_t days = 0;
    int year = 1900 + tm->tm_year;

    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    /* Add days for months in current year */
    for (int m = 0; m < tm->tm_mon; m++) {
        days += month_days(m, year);
    }

    /* Add remaining days */
    days += tm->tm_mday - 1;

    time_t result = days * 86400L;
    result += tm->tm_hour * 3600;
    result += tm->tm_min * 60;
    result += tm->tm_sec;

    return result;
}

/**
 * @brief Format time into a string.
 *
 * @details
 * Supported format specifiers:
 * - %Y: 4-digit year
 * - %m: Month (01-12)
 * - %d: Day of month (01-31)
 * - %H: Hour (00-23)
 * - %M: Minute (00-59)
 * - %S: Second (00-59)
 * - %%: Literal %
 *
 * @param s Buffer to store the formatted string.
 * @param max Maximum number of bytes to write.
 * @param format Format string.
 * @param tm Broken-down time.
 * @return Number of bytes written, or 0 on error.
 */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || max == 0 || !format || !tm)
        return 0;

    size_t written = 0;
    while (*format && written < max - 1) {
        if (*format == '%' && format[1]) {
            format++;
            char buf[8];
            int len = 0;

            switch (*format) {
                case 'Y': {
                    int yr = 1900 + tm->tm_year;
                    buf[0] = '0' + (yr / 1000) % 10;
                    buf[1] = '0' + (yr / 100) % 10;
                    buf[2] = '0' + (yr / 10) % 10;
                    buf[3] = '0' + yr % 10;
                    len = 4;
                    break;
                }
                case 'm':
                    len = 2;
                    buf[0] = '0' + ((tm->tm_mon + 1) / 10);
                    buf[1] = '0' + ((tm->tm_mon + 1) % 10);
                    break;
                case 'd':
                    len = 2;
                    buf[0] = '0' + (tm->tm_mday / 10);
                    buf[1] = '0' + (tm->tm_mday % 10);
                    break;
                case 'H':
                    len = 2;
                    buf[0] = '0' + (tm->tm_hour / 10);
                    buf[1] = '0' + (tm->tm_hour % 10);
                    break;
                case 'M':
                    len = 2;
                    buf[0] = '0' + (tm->tm_min / 10);
                    buf[1] = '0' + (tm->tm_min % 10);
                    break;
                case 'S':
                    len = 2;
                    buf[0] = '0' + (tm->tm_sec / 10);
                    buf[1] = '0' + (tm->tm_sec % 10);
                    break;
                case '%':
                    len = 1;
                    buf[0] = '%';
                    break;
                default:
                    s[written++] = '%';
                    if (written < max - 1)
                        s[written++] = *format;
                    format++;
                    continue;
            }

            for (int i = 0; i < len && written < max - 1; i++) {
                s[written++] = buf[i];
            }
            format++;
        } else {
            s[written++] = *format++;
        }
    }

    s[written] = '\0';
    return written;
}
