//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/locale.c
// Purpose: Localization functions for ViperOS libc.
// Key invariants: C/POSIX locale only; minimal implementation.
// Ownership/Lifetime: Library; static locale data.
// Links: user/libc/include/locale.h
//
//===----------------------------------------------------------------------===//

/**
 * @file locale.c
 * @brief Localization functions for ViperOS libc.
 *
 * @details
 * This file provides minimal locale support:
 *
 * - setlocale: Set or query current locale (C/POSIX only)
 * - localeconv: Get numeric formatting conventions
 *
 * ViperOS only supports the "C" and "POSIX" locales, which are
 * identical. International locale support is not implemented.
 * The lconv structure contains the default C locale values.
 */

#include "../include/locale.h"
#include "../include/string.h"

/* Current locale name for each category */
static char *current_locales[LC_MAX + 1] = {
    "C", /* LC_ALL */
    "C", /* LC_COLLATE */
    "C", /* LC_CTYPE */
    "C", /* LC_MESSAGES */
    "C", /* LC_MONETARY */
    "C", /* LC_NUMERIC */
    "C", /* LC_TIME */
};

/* Static lconv structure for C locale */
static struct lconv c_lconv = {
    /* Numeric formatting */
    .decimal_point = ".",
    .thousands_sep = "",
    .grouping = "",

    /* Monetary formatting */
    .int_curr_symbol = "",
    .currency_symbol = "",
    .mon_decimal_point = "",
    .mon_thousands_sep = "",
    .mon_grouping = "",
    .positive_sign = "",
    .negative_sign = "",
    .int_frac_digits = CHAR_MAX,
    .frac_digits = CHAR_MAX,
    .p_cs_precedes = CHAR_MAX,
    .p_sep_by_space = CHAR_MAX,
    .n_cs_precedes = CHAR_MAX,
    .n_sep_by_space = CHAR_MAX,
    .p_sign_posn = CHAR_MAX,
    .n_sign_posn = CHAR_MAX,

    /* POSIX extensions */
    .int_p_cs_precedes = CHAR_MAX,
    .int_p_sep_by_space = CHAR_MAX,
    .int_n_cs_precedes = CHAR_MAX,
    .int_n_sep_by_space = CHAR_MAX,
    .int_p_sign_posn = CHAR_MAX,
    .int_n_sign_posn = CHAR_MAX,
};

/*
 * setlocale - Set or query the locale
 *
 * Only "C", "POSIX", and "" (default) are supported.
 */
char *setlocale(int category, const char *locale)
{
    /* Validate category */
    if (category < LC_ALL || category > LC_MAX)
    {
        return (char *)0;
    }

    /* Query current locale */
    if (locale == (char *)0)
    {
        return current_locales[category];
    }

    /* Empty string means use default (C locale) */
    if (locale[0] == '\0')
    {
        locale = "C";
    }

    /* Only C and POSIX locales are supported */
    if (strcmp(locale, "C") != 0 && strcmp(locale, "POSIX") != 0)
    {
        return (char *)0;
    }

    /* Set locale for specified category */
    if (category == LC_ALL)
    {
        /* Set all categories */
        for (int i = LC_ALL; i <= LC_MAX; i++)
        {
            current_locales[i] = "C";
        }
    }
    else
    {
        current_locales[category] = "C";
    }

    return "C";
}

/*
 * localeconv - Get numeric formatting conventions
 *
 * Returns the C locale conventions (always).
 */
struct lconv *localeconv(void)
{
    return &c_lconv;
}
