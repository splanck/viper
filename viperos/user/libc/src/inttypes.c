//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/inttypes.c
// Purpose: Integer type conversion functions for ViperOS libc.
// Key invariants: Handles intmax_t/uintmax_t conversions.
// Ownership/Lifetime: Library; stateless functions.
// Links: user/libc/include/inttypes.h
//
//===----------------------------------------------------------------------===//

/**
 * @file inttypes.c
 * @brief Integer type conversion functions for ViperOS libc.
 *
 * @details
 * This file implements C99 integer type functions:
 *
 * - imaxabs: Absolute value of intmax_t
 * - imaxdiv: Division with quotient and remainder
 * - strtoimax: Parse string to intmax_t
 * - strtoumax: Parse string to uintmax_t
 *
 * These functions operate on maximum-width integer types,
 * providing portable handling of the largest available integers.
 */

#include "../include/inttypes.h"

/**
 * @brief Compute absolute value of a maximum-width integer.
 *
 * @param j The integer value.
 * @return The absolute value of j.
 */
intmax_t imaxabs(intmax_t j)
{
    return (j < 0) ? -j : j;
}

/* Division with quotient and remainder for maximum-width integers */
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom)
{
    imaxdiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

/* Parse string to intmax_t */
intmax_t strtoimax(const char *restrict nptr, char **restrict endptr, int base)
{
    /* Skip whitespace */
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r' || *nptr == '\f' ||
           *nptr == '\v')
    {
        nptr++;
    }

    /* Handle sign */
    int negative = 0;
    if (*nptr == '-')
    {
        negative = 1;
        nptr++;
    }
    else if (*nptr == '+')
    {
        nptr++;
    }

    /* Handle base prefix */
    if (base == 0)
    {
        if (*nptr == '0')
        {
            nptr++;
            if (*nptr == 'x' || *nptr == 'X')
            {
                base = 16;
                nptr++;
            }
            else
            {
                base = 8;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16 && *nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X'))
    {
        nptr += 2;
    }

    intmax_t result = 0;
    const char *start = nptr;

    while (*nptr)
    {
        int digit;
        if (*nptr >= '0' && *nptr <= '9')
        {
            digit = *nptr - '0';
        }
        else if (*nptr >= 'a' && *nptr <= 'z')
        {
            digit = *nptr - 'a' + 10;
        }
        else if (*nptr >= 'A' && *nptr <= 'Z')
        {
            digit = *nptr - 'A' + 10;
        }
        else
        {
            break;
        }

        if (digit >= base)
        {
            break;
        }

        result = result * base + digit;
        nptr++;
    }

    if (endptr)
    {
        *endptr = (char *)(nptr == start ? (char *)start - (negative || (*start == '+')) : nptr);
    }

    return negative ? -result : result;
}

/* Parse string to uintmax_t */
uintmax_t strtoumax(const char *restrict nptr, char **restrict endptr, int base)
{
    /* Skip whitespace */
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r' || *nptr == '\f' ||
           *nptr == '\v')
    {
        nptr++;
    }

    /* Handle optional + sign (ignore - for unsigned) */
    if (*nptr == '+')
    {
        nptr++;
    }

    /* Handle base prefix */
    if (base == 0)
    {
        if (*nptr == '0')
        {
            nptr++;
            if (*nptr == 'x' || *nptr == 'X')
            {
                base = 16;
                nptr++;
            }
            else
            {
                base = 8;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16 && *nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X'))
    {
        nptr += 2;
    }

    uintmax_t result = 0;
    const char *start = nptr;

    while (*nptr)
    {
        int digit;
        if (*nptr >= '0' && *nptr <= '9')
        {
            digit = *nptr - '0';
        }
        else if (*nptr >= 'a' && *nptr <= 'z')
        {
            digit = *nptr - 'a' + 10;
        }
        else if (*nptr >= 'A' && *nptr <= 'Z')
        {
            digit = *nptr - 'A' + 10;
        }
        else
        {
            break;
        }

        if (digit >= base)
        {
            break;
        }

        result = result * base + digit;
        nptr++;
    }

    if (endptr)
    {
        *endptr = (char *)(nptr == start ? (char *)start : nptr);
    }

    return result;
}
