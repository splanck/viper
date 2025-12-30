#ifndef _TIME_H
#define _TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

/* clock_t - type for clock() return value */
typedef long clock_t;

/* time_t - type for time values (seconds since epoch) */
typedef long time_t;

/* timespec - nanosecond precision time */
struct timespec {
    time_t tv_sec;  /* Seconds */
    long tv_nsec;   /* Nanoseconds */
};

/* timeval - microsecond precision time (for compatibility) */
struct timeval {
    time_t tv_sec;       /* Seconds */
    long tv_usec;        /* Microseconds */
};

/* tm - broken-down time structure */
struct tm {
    int tm_sec;    /* Seconds [0,60] */
    int tm_min;    /* Minutes [0,59] */
    int tm_hour;   /* Hours [0,23] */
    int tm_mday;   /* Day of month [1,31] */
    int tm_mon;    /* Month [0,11] */
    int tm_year;   /* Years since 1900 */
    int tm_wday;   /* Day of week [0,6] (Sunday=0) */
    int tm_yday;   /* Day of year [0,365] */
    int tm_isdst;  /* Daylight saving flag */
};

/* CLOCKS_PER_SEC - number of clock() ticks per second */
#define CLOCKS_PER_SEC 1000

/* Clock IDs for clock_gettime/clock_settime */
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

typedef int clockid_t;

/* NULL */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Clock functions */
clock_t clock(void);
time_t time(time_t *tloc);
long difftime(time_t time1, time_t time0);

/* Sleep functions */
int nanosleep(const struct timespec *req, struct timespec *rem);

/* POSIX clock functions */
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int clock_getres(clockid_t clk_id, struct timespec *res);

/* BSD time function */
int gettimeofday(struct timeval *tv, void *tz);

/* Time conversion (stubs) */
struct tm *gmtime(const time_t *timep);
struct tm *localtime(const time_t *timep);
time_t mktime(struct tm *tm);

/* Time formatting (stub) */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

#ifdef __cplusplus
}
#endif

#endif /* _TIME_H */
