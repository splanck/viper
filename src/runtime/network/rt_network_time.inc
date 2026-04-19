#pragma once

#include <stdint.h>
#include <time.h>

static inline int64_t rt_network_days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static inline time_t rt_network_timegm_utc(const struct tm *tm_val) {
    if (!tm_val || tm_val->tm_mon < 0 || tm_val->tm_mon > 11 || tm_val->tm_mday < 1 ||
        tm_val->tm_mday > 31 || tm_val->tm_hour < 0 || tm_val->tm_hour > 23 || tm_val->tm_min < 0 ||
        tm_val->tm_min > 59 || tm_val->tm_sec < 0 || tm_val->tm_sec > 60) {
        return (time_t)-1;
    }

    int64_t seconds = rt_network_days_from_civil(tm_val->tm_year + 1900,
                                                 (unsigned)(tm_val->tm_mon + 1),
                                                 (unsigned)tm_val->tm_mday) *
                          86400 +
                      (int64_t)tm_val->tm_hour * 3600 + (int64_t)tm_val->tm_min * 60 +
                      (int64_t)tm_val->tm_sec;
    time_t out = (time_t)seconds;
    return (int64_t)out == seconds ? out : (time_t)-1;
}
