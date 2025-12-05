//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_datetime.c
// Purpose: Runtime functions for date/time operations.
//
//===----------------------------------------------------------------------===//

#include "rt_datetime.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <sys/time.h>
#endif

int64_t rt_datetime_now(void) {
    return (int64_t)time(NULL);
}

int64_t rt_datetime_now_ms(void) {
#ifdef __APPLE__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

int64_t rt_datetime_year(int64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    return tm ? (int64_t)(tm->tm_year + 1900) : 0;
}

int64_t rt_datetime_month(int64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    return tm ? (int64_t)(tm->tm_mon + 1) : 0;
}

int64_t rt_datetime_day(int64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    return tm ? (int64_t)tm->tm_mday : 0;
}

int64_t rt_datetime_hour(int64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    return tm ? (int64_t)tm->tm_hour : 0;
}

int64_t rt_datetime_minute(int64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    return tm ? (int64_t)tm->tm_min : 0;
}

int64_t rt_datetime_second(int64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    return tm ? (int64_t)tm->tm_sec : 0;
}

int64_t rt_datetime_day_of_week(int64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    return tm ? (int64_t)tm->tm_wday : 0;
}

rt_string rt_datetime_format(int64_t timestamp, rt_string format) {
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    if (!tm) {
        return rt_string_from_bytes("", 0);
    }

    // Get format string as C string
    const char *fmt_cstr = rt_string_cstr(format);
    if (!fmt_cstr || *fmt_cstr == '\0') {
        return rt_string_from_bytes("", 0);
    }

    // Format the time
    char buffer[256];
    size_t len = strftime(buffer, sizeof(buffer), fmt_cstr, tm);

    if (len == 0) {
        return rt_string_from_bytes("", 0);
    }

    return rt_string_from_bytes(buffer, len);
}

rt_string rt_datetime_to_iso(int64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm = gmtime(&t);  // Use UTC for ISO format
    if (!tm) {
        return rt_string_from_bytes("", 0);
    }

    char buffer[32];
    int len = snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                       tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                       tm->tm_hour, tm->tm_min, tm->tm_sec);

    return rt_string_from_bytes(buffer, (size_t)len);
}

int64_t rt_datetime_create(int64_t year, int64_t month, int64_t day,
                           int64_t hour, int64_t minute, int64_t second) {
    struct tm tm = {0};
    tm.tm_year = (int)(year - 1900);
    tm.tm_mon = (int)(month - 1);
    tm.tm_mday = (int)day;
    tm.tm_hour = (int)hour;
    tm.tm_min = (int)minute;
    tm.tm_sec = (int)second;
    tm.tm_isdst = -1;  // Let the system determine DST

    time_t t = mktime(&tm);
    return (int64_t)t;
}

int64_t rt_datetime_add_seconds(int64_t timestamp, int64_t seconds) {
    return timestamp + seconds;
}

int64_t rt_datetime_add_days(int64_t timestamp, int64_t days) {
    return timestamp + (days * 86400);  // 86400 seconds per day
}

int64_t rt_datetime_diff(int64_t ts1, int64_t ts2) {
    return ts1 - ts2;
}
