/*
 * ViperOS libc - monetary.h
 * Monetary value formatting
 */

#ifndef _MONETARY_H
#define _MONETARY_H

#include "sys/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * strfmon - Format monetary value
     *
     * Formats a monetary value according to the current locale settings.
     *
     * Format specifiers:
     *   %n  - National currency format (locale-specific)
     *   %i  - International currency format (ISO 4217)
     *   %%  - Literal percent sign
     *
     * Optional flags (between % and conversion):
     *   =f  - Use 'f' as fill character (default: space)
     *   ^   - Don't group with thousands separator
     *   (   - Enclose negative amounts in parentheses
     *   +   - Use +/- sign
     *   !   - Suppress currency symbol
     *   -   - Left-justify
     *   w   - Minimum field width
     *   #n  - Left precision (digits before decimal)
     *   .p  - Right precision (digits after decimal)
     *
     * @s: Buffer to store formatted string
     * @maxsize: Maximum size of buffer
     * @format: Format string
     * @...: Values to format (double)
     *
     * Returns number of bytes written (excluding null terminator),
     * or -1 on error.
     */
    ssize_t strfmon(char *s, size_t maxsize, const char *format, ...);

    /*
     * strfmon_l - Format monetary value with explicit locale
     *
     * Like strfmon() but uses specified locale instead of current.
     *
     * @s: Buffer to store formatted string
     * @maxsize: Maximum size of buffer
     * @locale: Locale to use (NULL for current)
     * @format: Format string
     * @...: Values to format
     *
     * Returns number of bytes written, or -1 on error.
     */
    ssize_t strfmon_l(char *s, size_t maxsize, void *locale, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* _MONETARY_H */
