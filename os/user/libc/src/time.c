#include "../include/time.h"

/* Syscall helpers - defined in syscall.S */
extern long __syscall1(long num, long arg0);

/* Syscall numbers */
#define SYS_TIME_NOW 0x30
#define SYS_SLEEP 0x31

clock_t clock(void)
{
    /* Returns time in milliseconds since boot */
    return (clock_t)__syscall1(SYS_TIME_NOW, 0);
}

time_t time(time_t *tloc)
{
    /* ViperOS doesn't have real-time clock yet, return uptime in seconds */
    clock_t ms = clock();
    time_t t = ms / 1000;
    if (tloc)
        *tloc = t;
    return t;
}

/* Note: returns integer difference since ViperOS doesn't support FP in kernel */
long difftime(time_t time1, time_t time0)
{
    return (long)(time1 - time0);
}

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

int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (!tp)
        return -1;

    /* Both CLOCK_REALTIME and CLOCK_MONOTONIC return uptime for now */
    /* (ViperOS doesn't have a real-time clock) */
    if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
        return -1;

    /* Get time in milliseconds since boot */
    long ms = __syscall1(SYS_TIME_NOW, 0);

    tp->tv_sec = ms / 1000;
    tp->tv_nsec = (ms % 1000) * 1000000L;

    return 0;
}

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

/* Static storage for gmtime/localtime results */
static struct tm _tm_result;

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

struct tm *localtime(const time_t *timep)
{
    /* No timezone support, just use gmtime */
    return gmtime(timep);
}

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
