//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/time.c
// Purpose: Time and date functions for ViperDOS libc.
// Key invariants: Time measured as milliseconds since boot (no RTC).
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
 * Note: ViperDOS currently lacks a real-time clock (RTC), so all time
 * values represent milliseconds since boot rather than calendar time.
 * The gmtime/localtime functions provide a simplified approximation.
 */

#include "../include/time.h"

/* Syscall helpers - defined in syscall.S */
extern long __syscall1(long num, long arg0);

/* Syscall numbers */
#define SYS_TIME_NOW 0x30
#define SYS_SLEEP 0x31

/**
 * @brief Get processor time used.
 *
 * @details
 * Returns the time in milliseconds since system boot. In ViperDOS,
 * this is the same as wall-clock time since there's no distinction
 * between user and system time.
 *
 * @return Time in milliseconds since boot (CLOCKS_PER_SEC = 1000).
 */
clock_t clock(void)
{
    /* Returns time in milliseconds since boot */
    return (clock_t)__syscall1(SYS_TIME_NOW, 0);
}

/**
 * @brief Get current time in seconds.
 *
 * @details
 * Returns the current time as seconds since boot (not Unix epoch).
 * If tloc is not NULL, the time is also stored there.
 *
 * @param tloc If non-NULL, receives the time value.
 * @return Current time in seconds since boot.
 */
time_t time(time_t *tloc)
{
    /* ViperDOS doesn't have real-time clock yet, return uptime in seconds */
    clock_t ms = clock();
    time_t t = ms / 1000;
    if (tloc)
        *tloc = t;
    return t;
}

/**
 * @brief Compute difference between two times.
 *
 * @details
 * Returns the difference in seconds between time1 and time0.
 * Standard C returns double, but ViperDOS returns long to avoid
 * kernel floating-point dependencies.
 *
 * @param time1 Later time value.
 * @param time0 Earlier time value.
 * @return Difference (time1 - time0) in seconds.
 */
long difftime(time_t time1, time_t time0)
{
    return (long)(time1 - time0);
}

/**
 * @brief High-resolution sleep.
 *
 * @details
 * Suspends execution for the time specified in req. The actual
 * resolution is milliseconds (any nanosecond component is rounded up).
 * If rem is non-NULL, any remaining time would be stored there on
 * interruption (currently always 0 as interruption isn't supported).
 *
 * @param req Requested sleep duration.
 * @param rem If non-NULL, receives remaining time on interruption.
 * @return 0 on success, -1 on error.
 */
int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req)
        return -1;

    /* Convert to milliseconds (minimum 1ms if any time requested) */
    long ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    if (ms == 0 && (req->tv_sec > 0 || req->tv_nsec > 0))
        ms = 1;

    __syscall1(SYS_SLEEP, ms);

    /* We don't support interruption, so remaining time is always 0 */
    if (rem)
    {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}

/**
 * @brief Get time from a specified clock.
 *
 * @details
 * Retrieves the current time from the specified clock and stores it in tp.
 * ViperDOS supports CLOCK_REALTIME and CLOCK_MONOTONIC, both returning
 * uptime since system boot (no real-time clock available).
 *
 * @param clk_id Clock to query (CLOCK_REALTIME or CLOCK_MONOTONIC).
 * @param tp Structure to receive the time value.
 * @return 0 on success, -1 on error (invalid clock or NULL tp).
 */
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (!tp)
        return -1;

    /* Both CLOCK_REALTIME and CLOCK_MONOTONIC return uptime for now */
    /* (ViperDOS doesn't have a real-time clock) */
    if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
        return -1;

    /* Get time in milliseconds since boot */
    long ms = __syscall1(SYS_TIME_NOW, 0);

    tp->tv_sec = ms / 1000;
    tp->tv_nsec = (ms % 1000) * 1000000L;

    return 0;
}

/**
 * @brief Get clock resolution.
 *
 * @details
 * Retrieves the resolution (precision) of the specified clock. In ViperDOS,
 * all clocks have millisecond resolution (1,000,000 nanoseconds).
 *
 * @param clk_id Clock to query (CLOCK_REALTIME or CLOCK_MONOTONIC).
 * @param res If non-NULL, receives the clock resolution.
 * @return 0 on success, -1 on invalid clock ID.
 */
int clock_getres(clockid_t clk_id, struct timespec *res)
{
    if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
        return -1;

    if (res)
    {
        /* Resolution is 1 millisecond */
        res->tv_sec = 0;
        res->tv_nsec = 1000000L;
    }

    return 0;
}

/**
 * @brief Get current time with microsecond precision.
 *
 * @details
 * Returns the current time in seconds and microseconds. In ViperDOS,
 * this is uptime since boot rather than calendar time. The timezone
 * parameter is ignored.
 *
 * @param tv Structure to receive the time value.
 * @param tz Timezone (ignored, pass NULL).
 * @return 0 on success, -1 if tv is NULL.
 */
int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz; /* Timezone not supported */

    if (!tv)
        return -1;

    /* Get time in milliseconds since boot */
    long ms = __syscall1(SYS_TIME_NOW, 0);

    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000L;

    return 0;
}

/** Static storage for gmtime/localtime results (non-reentrant). */
static struct tm _tm_result;

/**
 * @brief Convert time_t to broken-down time (UTC).
 *
 * @details
 * Converts the calendar time pointed to by timep into a broken-down time
 * structure (struct tm) expressed in UTC. The result is stored in static
 * memory and overwritten by subsequent calls.
 *
 * @warning This implementation is a simplified approximation since ViperDOS
 * doesn't have a real-time clock. The time value is treated as uptime rather
 * than actual calendar time.
 *
 * @param timep Pointer to time_t value to convert.
 * @return Pointer to static struct tm, or NULL if timep is NULL.
 *
 * @note Not thread-safe; use gmtime_r() for reentrant version (if available).
 */
struct tm *gmtime(const time_t *timep)
{
    if (!timep)
        return (struct tm *)0;

    /* Simple implementation - doesn't handle real dates */
    /* Just return uptime as seconds/minutes/hours/days */
    time_t t = *timep;

    _tm_result.tm_sec = t % 60;
    t /= 60;
    _tm_result.tm_min = t % 60;
    t /= 60;
    _tm_result.tm_hour = t % 24;
    t /= 24;
    _tm_result.tm_mday = (t % 31) + 1;
    t /= 31;
    _tm_result.tm_mon = t % 12;
    t /= 12;
    _tm_result.tm_year = 70 + t; /* Years since 1900 */
    _tm_result.tm_wday = 0;
    _tm_result.tm_yday = 0;
    _tm_result.tm_isdst = 0;

    return &_tm_result;
}

/**
 * @brief Convert time_t to broken-down local time.
 *
 * @details
 * Converts the calendar time to local time. In ViperDOS, there is no
 * timezone support, so this is equivalent to gmtime().
 *
 * @param timep Pointer to time_t value to convert.
 * @return Pointer to static struct tm, or NULL if timep is NULL.
 *
 * @note Not thread-safe.
 */
struct tm *localtime(const time_t *timep)
{
    /* No timezone support, just use gmtime */
    return gmtime(timep);
}

/**
 * @brief Convert broken-down time to time_t.
 *
 * @details
 * Converts the broken-down time structure to a time_t value representing
 * the same time. This is the inverse of localtime()/gmtime().
 *
 * @warning This is a simplified implementation that doesn't handle real
 * calendar dates accurately (no leap years, varying month lengths, etc.).
 *
 * @param tm Pointer to struct tm to convert.
 * @return time_t representation, or -1 if tm is NULL.
 */
time_t mktime(struct tm *tm)
{
    if (!tm)
        return -1;

    /* Simple reverse of gmtime - not accurate for real dates */
    time_t result = 0;
    result += tm->tm_sec;
    result += tm->tm_min * 60;
    result += tm->tm_hour * 3600;
    result += (tm->tm_mday - 1) * 86400L;
    result += tm->tm_mon * 31L * 86400L;
    result += (tm->tm_year - 70) * 365L * 86400L;

    return result;
}

/**
 * @brief Format time into a string.
 *
 * @details
 * Formats the broken-down time tm according to the format string and stores
 * the result in s. The format string may contain literal characters and
 * conversion specifiers beginning with %.
 *
 * Supported format specifiers (minimal implementation):
 * - %H: Hour (00-23)
 * - %M: Minute (00-59)
 * - %S: Second (00-59)
 * - %%: Literal %
 *
 * Other format specifiers are copied to the output literally.
 *
 * @param s Buffer to store the formatted string.
 * @param max Maximum number of bytes to write (including null terminator).
 * @param format Format string with conversion specifiers.
 * @param tm Broken-down time to format.
 * @return Number of bytes written (excluding null terminator), or 0 on error.
 */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
    /* Minimal implementation - just copy format with some substitutions */
    if (!s || max == 0 || !format || !tm)
        return 0;

    size_t written = 0;
    while (*format && written < max - 1)
    {
        if (*format == '%' && format[1])
        {
            format++;
            char buf[16];
            const char *insert = buf;
            int len = 0;

            switch (*format)
            {
                case 'H':
                    len = 2;
                    buf[0] = '0' + (tm->tm_hour / 10);
                    buf[1] = '0' + (tm->tm_hour % 10);
                    buf[2] = '\0';
                    break;
                case 'M':
                    len = 2;
                    buf[0] = '0' + (tm->tm_min / 10);
                    buf[1] = '0' + (tm->tm_min % 10);
                    buf[2] = '\0';
                    break;
                case 'S':
                    len = 2;
                    buf[0] = '0' + (tm->tm_sec / 10);
                    buf[1] = '0' + (tm->tm_sec % 10);
                    buf[2] = '\0';
                    break;
                case '%':
                    len = 1;
                    buf[0] = '%';
                    buf[1] = '\0';
                    break;
                default:
                    /* Unknown format, copy as-is */
                    s[written++] = '%';
                    if (written < max - 1)
                        s[written++] = *format;
                    format++;
                    continue;
            }

            for (int i = 0; i < len && written < max - 1; i++)
            {
                s[written++] = insert[i];
            }
            format++;
        }
        else
        {
            s[written++] = *format++;
        }
    }

    s[written] = '\0';
    return written;
}
