/*
 * ViperOS libc - syslog.h
 * System logging interface
 */

#ifndef _SYSLOG_H
#define _SYSLOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Syslog priority levels */
#define LOG_EMERG 0   /* System is unusable */
#define LOG_ALERT 1   /* Action must be taken immediately */
#define LOG_CRIT 2    /* Critical conditions */
#define LOG_ERR 3     /* Error conditions */
#define LOG_WARNING 4 /* Warning conditions */
#define LOG_NOTICE 5  /* Normal but significant condition */
#define LOG_INFO 6    /* Informational */
#define LOG_DEBUG 7   /* Debug-level messages */

/* Extract priority from a combined priority/facility value */
#define LOG_PRIMASK 0x07
#define LOG_PRI(p) ((p) & LOG_PRIMASK)

/* Syslog facility values */
#define LOG_KERN (0 << 3)      /* Kernel messages */
#define LOG_USER (1 << 3)      /* Random user-level messages */
#define LOG_MAIL (2 << 3)      /* Mail system */
#define LOG_DAEMON (3 << 3)    /* System daemons */
#define LOG_AUTH (4 << 3)      /* Security/authorization messages */
#define LOG_SYSLOG (5 << 3)    /* Internal syslog messages */
#define LOG_LPR (6 << 3)       /* Line printer subsystem */
#define LOG_NEWS (7 << 3)      /* Network news subsystem */
#define LOG_UUCP (8 << 3)      /* UUCP subsystem */
#define LOG_CRON (9 << 3)      /* Clock daemon */
#define LOG_AUTHPRIV (10 << 3) /* Security/authorization (private) */
#define LOG_FTP (11 << 3)      /* FTP daemon */
#define LOG_LOCAL0 (16 << 3)   /* Reserved for local use */
#define LOG_LOCAL1 (17 << 3)
#define LOG_LOCAL2 (18 << 3)
#define LOG_LOCAL3 (19 << 3)
#define LOG_LOCAL4 (20 << 3)
#define LOG_LOCAL5 (21 << 3)
#define LOG_LOCAL6 (22 << 3)
#define LOG_LOCAL7 (23 << 3)

/* Number of facilities */
#define LOG_NFACILITIES 24

/* Extract facility from priority */
#define LOG_FACMASK 0x03F8
#define LOG_FAC(p) (((p) & LOG_FACMASK) >> 3)

/* Create a combined priority/facility value */
#define LOG_MAKEPRI(fac, pri) ((fac) | (pri))

/* openlog() option flags */
#define LOG_PID 0x01    /* Log the PID with each message */
#define LOG_CONS 0x02   /* Log on the console if errors sending to syslog */
#define LOG_ODELAY 0x04 /* Delay open until first syslog() (default) */
#define LOG_NDELAY 0x08 /* Don't delay open */
#define LOG_NOWAIT 0x10 /* Don't wait for child processes */
#define LOG_PERROR 0x20 /* Log to stderr as well */

/* setlogmask() helper */
#define LOG_MASK(pri) (1 << (pri))
#define LOG_UPTO(pri) ((1 << ((pri) + 1)) - 1)

    /*
     * openlog - Open connection to system logger
     *
     * ident: String prepended to every message
     * option: Logging options (LOG_PID, LOG_CONS, etc.)
     * facility: Default facility for messages
     */
    void openlog(const char *ident, int option, int facility);

    /*
     * syslog - Generate a log message
     *
     * priority: Message priority (LOG_ERR, LOG_INFO, etc.)
     * format: printf-style format string
     */
    void syslog(int priority, const char *format, ...);

    /*
     * vsyslog - Generate a log message (va_list version)
     */
    void vsyslog(int priority, const char *format, va_list ap);

    /*
     * closelog - Close connection to system logger
     */
    void closelog(void);

    /*
     * setlogmask - Set the log priority mask
     *
     * Returns the previous mask value.
     */
    int setlogmask(int mask);

#ifdef __cplusplus
}
#endif

#endif /* _SYSLOG_H */
