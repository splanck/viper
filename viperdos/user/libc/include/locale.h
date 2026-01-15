/*
 * ViperDOS libc - locale.h
 * Localization functions (minimal implementation)
 */

#ifndef _LOCALE_H
#define _LOCALE_H

#ifdef __cplusplus
extern "C"
{
#endif

/* NULL */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Locale category constants */
#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MESSAGES 3
#define LC_MONETARY 4
#define LC_NUMERIC 5
#define LC_TIME 6

/* Number of locale categories */
#define LC_MAX LC_TIME

    /* Locale information structure */
    struct lconv
    {
        /* Numeric (non-monetary) formatting */
        char *decimal_point; /* Radix character */
        char *thousands_sep; /* Thousands separator */
        char *grouping;      /* Size of digit groups */

        /* Monetary formatting */
        char *int_curr_symbol;   /* International currency symbol */
        char *currency_symbol;   /* Local currency symbol */
        char *mon_decimal_point; /* Monetary decimal point */
        char *mon_thousands_sep; /* Monetary thousands separator */
        char *mon_grouping;      /* Monetary grouping */
        char *positive_sign;     /* Positive sign */
        char *negative_sign;     /* Negative sign */
        char int_frac_digits;    /* International fractional digits */
        char frac_digits;        /* Local fractional digits */
        char p_cs_precedes;      /* Currency symbol precedes positive */
        char p_sep_by_space;     /* Space between positive and symbol */
        char n_cs_precedes;      /* Currency symbol precedes negative */
        char n_sep_by_space;     /* Space between negative and symbol */
        char p_sign_posn;        /* Position of positive sign */
        char n_sign_posn;        /* Position of negative sign */

        /* POSIX extensions */
        char int_p_cs_precedes;
        char int_p_sep_by_space;
        char int_n_cs_precedes;
        char int_n_sep_by_space;
        char int_p_sign_posn;
        char int_n_sign_posn;
    };

/* Value indicating no grouping */
#define CHAR_MAX 127

    /*
     * setlocale - Set or query the locale
     *
     * In this minimal implementation, only "C" and "POSIX" locales are supported.
     * All other locale names are rejected.
     */
    char *setlocale(int category, const char *locale);

    /*
     * localeconv - Get numeric formatting conventions
     *
     * Returns a pointer to a static lconv structure with the numeric
     * formatting conventions for the current locale.
     */
    struct lconv *localeconv(void);

#ifdef __cplusplus
}
#endif

#endif /* _LOCALE_H */
