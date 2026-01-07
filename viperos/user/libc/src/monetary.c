//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/monetary.c
// Purpose: Monetary formatting functions for ViperOS libc.
// Key invariants: C locale only; USD/$ defaults; strfmon format string.
// Ownership/Lifetime: Library; stateless formatting.
// Links: user/libc/include/monetary.h
//
//===----------------------------------------------------------------------===//

/**
 * @file monetary.c
 * @brief Monetary formatting functions for ViperOS libc.
 *
 * @details
 * This file implements POSIX monetary formatting:
 *
 * - strfmon: Format monetary values according to format string
 * - strfmon_l: Format with explicit locale (locale ignored)
 *
 * Format string conversion specifiers:
 * - %n: National currency format (e.g., "$1,234.56")
 * - %i: International currency format (e.g., "USD 1,234.56")
 *
 * Format flags:
 * - =f: Fill character (default space)
 * - ^: No grouping separators
 * - (: Negative values in parentheses
 * - +: Show explicit sign
 * - !: Suppress currency symbol
 * - -: Left justify
 *
 * ViperOS uses C locale defaults: USD, $, comma separators.
 */

#include "../include/monetary.h"
#include "../include/ctype.h"
#include "../include/errno.h"
#include "../include/math.h"
#include "../include/stdarg.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* Default currency symbol */
#define DEFAULT_CURRENCY "$"
#define DEFAULT_INT_CURRENCY "USD "

/* Default decimal places for currency */
#define DEFAULT_FRAC_DIGITS 2

/*
 * Helper: Format a single monetary value
 */
static ssize_t format_monetary(char *buf,
                               size_t maxsize,
                               double value,
                               int international,
                               int no_symbol,
                               int left_justify,
                               int paren_negative,
                               int show_sign,
                               char fill_char,
                               int no_grouping,
                               int width,
                               int left_prec,
                               int right_prec)
{
    if (maxsize == 0)
        return -1;

    char temp[256];
    size_t temp_len = 0;
    int negative = value < 0;
    if (negative)
        value = -value;

    /* Default precision */
    if (right_prec < 0)
        right_prec = DEFAULT_FRAC_DIGITS;

    /* Format the number */
    char num_buf[128];
    int num_len = snprintf(num_buf, sizeof(num_buf), "%.*f", right_prec, value);
    if (num_len < 0 || (size_t)num_len >= sizeof(num_buf))
    {
        errno = EINVAL;
        return -1;
    }

    /* Find decimal point */
    char *decimal = strchr(num_buf, '.');
    size_t int_part_len = decimal ? (size_t)(decimal - num_buf) : strlen(num_buf);

    /* Apply left precision (padding with fill character) */
    if (left_prec > 0 && (int)int_part_len < left_prec)
    {
        int pad = left_prec - (int)int_part_len;
        for (int i = 0; i < pad && temp_len < sizeof(temp) - 1; i++)
        {
            temp[temp_len++] = fill_char;
        }
    }

    /* Add sign or opening parenthesis for negative */
    if (negative)
    {
        if (paren_negative)
        {
            if (temp_len < sizeof(temp) - 1)
                temp[temp_len++] = '(';
        }
        else if (show_sign)
        {
            if (temp_len < sizeof(temp) - 1)
                temp[temp_len++] = '-';
        }
        else
        {
            if (temp_len < sizeof(temp) - 1)
                temp[temp_len++] = '-';
        }
    }
    else if (show_sign)
    {
        if (temp_len < sizeof(temp) - 1)
            temp[temp_len++] = '+';
    }

    /* Add currency symbol */
    if (!no_symbol)
    {
        const char *sym = international ? DEFAULT_INT_CURRENCY : DEFAULT_CURRENCY;
        while (*sym && temp_len < sizeof(temp) - 1)
        {
            temp[temp_len++] = *sym++;
        }
    }

    /* Copy integer part with optional grouping */
    if (!no_grouping && int_part_len > 3)
    {
        /* Add thousands separators */
        int group_start = int_part_len % 3;
        if (group_start == 0)
            group_start = 3;

        for (size_t i = 0; i < int_part_len && temp_len < sizeof(temp) - 1; i++)
        {
            if (i > 0 && (i - group_start) % 3 == 0)
            {
                if (temp_len < sizeof(temp) - 1)
                    temp[temp_len++] = ',';
            }
            temp[temp_len++] = num_buf[i];
        }
    }
    else
    {
        /* No grouping */
        for (size_t i = 0; i < int_part_len && temp_len < sizeof(temp) - 1; i++)
        {
            temp[temp_len++] = num_buf[i];
        }
    }

    /* Copy decimal part */
    if (decimal)
    {
        while (*decimal && temp_len < sizeof(temp) - 1)
        {
            temp[temp_len++] = *decimal++;
        }
    }

    /* Add closing parenthesis for negative */
    if (negative && paren_negative)
    {
        if (temp_len < sizeof(temp) - 1)
            temp[temp_len++] = ')';
    }

    temp[temp_len] = '\0';

    /* Apply field width */
    size_t final_len = temp_len;
    if (width > 0 && (size_t)width > temp_len)
    {
        final_len = width;
    }

    if (final_len >= maxsize)
    {
        errno = E2BIG;
        return -1;
    }

    /* Copy to output with justification */
    if (left_justify)
    {
        memcpy(buf, temp, temp_len);
        for (size_t i = temp_len; i < final_len; i++)
        {
            buf[i] = ' ';
        }
    }
    else
    {
        for (size_t i = 0; i < final_len - temp_len; i++)
        {
            buf[i] = ' ';
        }
        memcpy(buf + (final_len - temp_len), temp, temp_len);
    }
    buf[final_len] = '\0';

    return (ssize_t)final_len;
}

/*
 * strfmon - Format monetary value
 */
ssize_t strfmon(char *s, size_t maxsize, const char *format, ...)
{
    if (!s || !format || maxsize == 0)
    {
        errno = EINVAL;
        return -1;
    }

    va_list args;
    va_start(args, format);

    char *out = s;
    size_t remaining = maxsize;
    const char *p = format;

    while (*p && remaining > 1)
    {
        if (*p != '%')
        {
            *out++ = *p++;
            remaining--;
            continue;
        }

        p++; /* Skip '%' */

        /* Handle %% */
        if (*p == '%')
        {
            *out++ = '%';
            p++;
            remaining--;
            continue;
        }

        /* Parse format flags and modifiers */
        char fill_char = ' ';
        int no_grouping = 0;
        int paren_negative = 0;
        int show_sign = 0;
        int no_symbol = 0;
        int left_justify = 0;
        int width = 0;
        int left_prec = -1;
        int right_prec = -1;

        /* Parse flags */
        while (*p)
        {
            if (*p == '=')
            {
                p++;
                if (*p)
                    fill_char = *p++;
            }
            else if (*p == '^')
            {
                no_grouping = 1;
                p++;
            }
            else if (*p == '(')
            {
                paren_negative = 1;
                p++;
            }
            else if (*p == '+')
            {
                show_sign = 1;
                p++;
            }
            else if (*p == '!')
            {
                no_symbol = 1;
                p++;
            }
            else if (*p == '-')
            {
                left_justify = 1;
                p++;
            }
            else
            {
                break;
            }
        }

        /* Parse width */
        while (isdigit((unsigned char)*p))
        {
            width = width * 10 + (*p - '0');
            p++;
        }

        /* Parse left precision */
        if (*p == '#')
        {
            p++;
            left_prec = 0;
            while (isdigit((unsigned char)*p))
            {
                left_prec = left_prec * 10 + (*p - '0');
                p++;
            }
        }

        /* Parse right precision */
        if (*p == '.')
        {
            p++;
            right_prec = 0;
            while (isdigit((unsigned char)*p))
            {
                right_prec = right_prec * 10 + (*p - '0');
                p++;
            }
        }

        /* Parse conversion specifier */
        int international = 0;
        if (*p == 'i')
        {
            international = 1;
            p++;
        }
        else if (*p == 'n')
        {
            international = 0;
            p++;
        }
        else
        {
            /* Invalid specifier */
            errno = EINVAL;
            va_end(args);
            return -1;
        }

        /* Get the value and format it */
        double value = va_arg(args, double);

        ssize_t written = format_monetary(out,
                                          remaining,
                                          value,
                                          international,
                                          no_symbol,
                                          left_justify,
                                          paren_negative,
                                          show_sign,
                                          fill_char,
                                          no_grouping,
                                          width,
                                          left_prec,
                                          right_prec);
        if (written < 0)
        {
            va_end(args);
            return -1;
        }

        out += written;
        remaining -= written;
    }

    *out = '\0';
    va_end(args);

    return out - s;
}

/*
 * strfmon_l - Format monetary value with explicit locale
 */
ssize_t strfmon_l(char *s, size_t maxsize, void *locale, const char *format, ...)
{
    (void)locale; /* ViperOS doesn't support locales yet */

    if (!s || !format || maxsize == 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* Just call strfmon with the remaining args */
    va_list args;
    va_start(args, format);

    /* We need to re-implement since we can't forward va_list easily */
    char *out = s;
    size_t remaining = maxsize;
    const char *p = format;

    while (*p && remaining > 1)
    {
        if (*p != '%')
        {
            *out++ = *p++;
            remaining--;
            continue;
        }

        p++;
        if (*p == '%')
        {
            *out++ = '%';
            p++;
            remaining--;
            continue;
        }

        /* Simplified parsing - just handle basic %n and %i */
        int international = 0;
        while (*p && *p != 'n' && *p != 'i')
            p++;

        if (*p == 'i')
        {
            international = 1;
            p++;
        }
        else if (*p == 'n')
        {
            p++;
        }
        else
        {
            break;
        }

        double value = va_arg(args, double);
        ssize_t written =
            format_monetary(out, remaining, value, international, 0, 0, 0, 0, ' ', 0, 0, -1, -1);
        if (written < 0)
        {
            va_end(args);
            return -1;
        }

        out += written;
        remaining -= written;
    }

    *out = '\0';
    va_end(args);

    return out - s;
}
