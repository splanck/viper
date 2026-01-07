//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/syslog.c
// Purpose: System logging functions for ViperOS libc.
// Key invariants: Outputs to stderr; no syslogd daemon.
// Ownership/Lifetime: Library; static logging state.
// Links: user/libc/include/syslog.h
//
//===----------------------------------------------------------------------===//

/**
 * @file syslog.c
 * @brief System logging functions for ViperOS libc.
 *
 * @details
 * This file implements BSD/POSIX syslog functions:
 *
 * - openlog: Configure logging (ident, options, facility)
 * - closelog: Close logging connection
 * - syslog/vsyslog: Generate a log message
 * - setlogmask: Set priority filter mask
 *
 * ViperOS does not have a syslogd daemon. All log messages are
 * formatted with timestamp, priority, and ident, then written
 * to stderr. The LOG_CONS, LOG_PID, and LOG_PERROR options are
 * partially supported. Priority filtering via setlogmask works.
 */

#include "../include/syslog.h"
#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/time.h"
#include "../include/unistd.h"

/* Syslog state */
static const char *log_ident = NULL;
static int log_option = 0;
static int log_facility = LOG_USER;
static int log_mask = 0xFF; /* All priorities enabled by default */

/* Priority names */
static const char *priority_names[] = {
    "EMERG", "ALERT", "CRIT", "ERR", "WARNING", "NOTICE", "INFO", "DEBUG"};

/* Facility names - reserved for future use */
static const char *facility_names[] __attribute__((unused)) = {
    "kern",   "user",   "mail",     "daemon", "auth",   "syslog", "lpr",    "news",
    "uucp",   "cron",   "authpriv", "ftp",    "ntp",    "audit",  "alert",  "clock",
    "local0", "local1", "local2",   "local3", "local4", "local5", "local6", "local7"};

/*
 * openlog - Open connection to system logger
 */
void openlog(const char *ident, int option, int facility)
{
    log_ident = ident;
    log_option = option;
    log_facility = facility & LOG_FACMASK;

    /* If LOG_NDELAY is set, we would open the connection here */
    /* For ViperOS, we just print to console, so nothing to open */
}

/*
 * closelog - Close connection to system logger
 */
void closelog(void)
{
    log_ident = NULL;
    log_option = 0;
    log_facility = LOG_USER;
}

/*
 * setlogmask - Set the log priority mask
 */
int setlogmask(int mask)
{
    int old_mask = log_mask;
    if (mask != 0)
    {
        log_mask = mask;
    }
    return old_mask;
}

/*
 * vsyslog - Generate a log message (va_list version)
 */
void vsyslog(int priority, const char *format, va_list ap)
{
    int pri = LOG_PRI(priority);
    int fac = priority & LOG_FACMASK;

    /* If no facility specified, use default */
    if (fac == 0)
    {
        fac = log_facility;
    }

    /* Check if this priority is enabled */
    if (!(log_mask & LOG_MASK(pri)))
    {
        return;
    }

    /* Build the log message */
    char buffer[1024];
    char *p = buffer;
    char *end = buffer + sizeof(buffer) - 2;

    /* Add timestamp */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm)
    {
        static const char *months[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int len = snprintf(p,
                           end - p,
                           "%s %2d %02d:%02d:%02d ",
                           months[tm->tm_mon],
                           tm->tm_mday,
                           tm->tm_hour,
                           tm->tm_min,
                           tm->tm_sec);
        if (len > 0)
            p += len;
    }

    /* Add ident if set */
    if (log_ident)
    {
        int len = snprintf(p, end - p, "%s", log_ident);
        if (len > 0)
            p += len;

        /* Add PID if requested */
        if (log_option & LOG_PID)
        {
            len = snprintf(p, end - p, "[%d]", (int)getpid());
            if (len > 0)
                p += len;
        }

        if (p < end)
            *p++ = ':';
        if (p < end)
            *p++ = ' ';
    }

    /* Add priority prefix */
    if (pri >= 0 && pri <= LOG_DEBUG)
    {
        int len = snprintf(p, end - p, "<%s> ", priority_names[pri]);
        if (len > 0)
            p += len;
    }

    /* Format the message */
    int remaining = end - p;
    if (remaining > 0)
    {
        /* Handle %m for errno message */
        /* For simplicity, just format directly */
        int len = vsnprintf(p, remaining, format, ap);
        if (len > 0 && len < remaining)
            p += len;
    }

    /* Ensure newline */
    if (p > buffer && *(p - 1) != '\n')
    {
        if (p < end)
            *p++ = '\n';
    }
    *p = '\0';

    /* Output the message */
    /* In ViperOS, we output to stderr/console */
    fputs(buffer, stderr);

    /* If LOG_PERROR is set, also output to stderr (already done above) */

    /* If LOG_CONS is set and we failed to write to syslog, write to console */
    /* For ViperOS, we always write to console anyway */
}

/*
 * syslog - Generate a log message
 */
void syslog(int priority, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}
