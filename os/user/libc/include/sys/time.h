/*
 * ViperOS C Library - sys/time.h
 * Time types and functions
 */

#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include "../time.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* timeval macros */
#define timerclear(tvp) ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timerisset(tvp) ((tvp)->tv_sec || (tvp)->tv_usec)
#define timercmp(a, b, CMP)                                                                        \
    (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec CMP(b)->tv_usec) : ((a)->tv_sec CMP(b)->tv_sec))

#define timeradd(a, b, result)                                                                     \
    do                                                                                             \
    {                                                                                              \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;                                              \
        (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;                                           \
        if ((result)->tv_usec >= 1000000)                                                          \
        {                                                                                          \
            ++(result)->tv_sec;                                                                    \
            (result)->tv_usec -= 1000000;                                                          \
        }                                                                                          \
    } while (0)

#define timersub(a, b, result)                                                                     \
    do                                                                                             \
    {                                                                                              \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                                              \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                                           \
        if ((result)->tv_usec < 0)                                                                 \
        {                                                                                          \
            --(result)->tv_sec;                                                                    \
            (result)->tv_usec += 1000000;                                                          \
        }                                                                                          \
    } while (0)

    /* itimerval - for interval timers */
    struct itimerval
    {
        struct timeval it_interval; /* Timer interval */
        struct timeval it_value;    /* Current value */
    };

/* Timer types */
#define ITIMER_REAL 0    /* Real-time timer (SIGALRM) */
#define ITIMER_VIRTUAL 1 /* Virtual timer (SIGVTALRM) */
#define ITIMER_PROF 2    /* Profiling timer (SIGPROF) */

    /* timezone structure (obsolete) */
    struct timezone
    {
        int tz_minuteswest; /* Minutes west of GMT */
        int tz_dsttime;     /* DST correction type */
    };

    /* Functions - gettimeofday is declared in time.h */
    int settimeofday(const struct timeval *tv, const struct timezone *tz);
    int getitimer(int which, struct itimerval *value);
    int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);
    int utimes(const char *filename, const struct timeval times[2]);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TIME_H */
