/*
 * ViperOS C Library - fmtmsg.h
 * Message display structures
 */

#ifndef _FMTMSG_H
#define _FMTMSG_H

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Classification component - severity
 * Identifies the source of the condition
 */
#define MM_HARD 0x001 /* Hardware source */
#define MM_SOFT 0x002 /* Software source */
#define MM_FIRM 0x004 /* Firmware source */

/*
 * Classification component - source subclassification
 */
#define MM_APPL 0x008  /* Application */
#define MM_UTIL 0x010  /* Utility */
#define MM_OPSYS 0x020 /* Operating system */

/*
 * Classification component - display subclassification
 */
#define MM_PRINT 0x100   /* Print to standard error */
#define MM_CONSOLE 0x200 /* Print to console device */

/*
 * Classification component - status subclassification
 */
#define MM_RECOVER 0x040 /* Recoverable error */
#define MM_NRECOV 0x080  /* Non-recoverable error */

/*
 * Severity levels for the severity argument
 */
#define MM_NOSEV 0   /* No severity level provided */
#define MM_HALT 1    /* Severe error - halt */
#define MM_ERROR 2   /* Error detected */
#define MM_WARNING 3 /* Warning condition */
#define MM_INFO 4    /* Informational message */

/*
 * Null values for optional arguments
 * Used to indicate that a component is not provided
 */
#define MM_NULLLBL ((char *)0) /* No label */
#define MM_NULLSEV 0           /* No severity */
#define MM_NULLMC ((char *)0)  /* No class */
#define MM_NULLTXT ((char *)0) /* No text */
#define MM_NULLACT ((char *)0) /* No action */
#define MM_NULLTAG ((char *)0) /* No tag */

/*
 * Return values from fmtmsg()
 */
#define MM_OK 0       /* All requested operations succeeded */
#define MM_NOTOK (-1) /* Complete failure */
#define MM_NOMSG 1    /* Unable to generate message on stderr */
#define MM_NOCON 2    /* Unable to generate message on console */

    /*
     * Display a formatted message.
     *
     * classification - Identifies source and handling
     * label - Identifies message source (max 10 chars)
     * severity - Indicates urgency
     * text - Describes the condition
     * action - Describes action to take
     * tag - Unique identifier (e.g., "PROJ:msgid")
     *
     * Returns:
     *   MM_OK     - Success
     *   MM_NOTOK  - Complete failure
     *   MM_NOMSG  - Failed to write to stderr
     *   MM_NOCON  - Failed to write to console
     */
    int fmtmsg(long classification,
               const char *label,
               int severity,
               const char *text,
               const char *action,
               const char *tag);

    /*
     * Add a severity level.
     *
     * severity - Severity value
     * string - Text to display for this severity
     *
     * Returns:
     *   MM_OK     - Success
     *   MM_NOTOK  - Failure
     */
    int addseverity(int severity, const char *string);

#ifdef __cplusplus
}
#endif

#endif /* _FMTMSG_H */
