/*
 * ViperDOS C Library - langinfo.h
 * Language information constants and nl_langinfo()
 */

#ifndef _LANGINFO_H
#define _LANGINFO_H

#include <nl_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* nl_item type for nl_langinfo() argument */
    typedef int nl_item;

/* LC_CTYPE category */
#define CODESET 0 /* Coded character set name (e.g., "UTF-8") */

/* LC_TIME category - date/time formatting */
#define D_T_FMT 1    /* Date and time format */
#define D_FMT 2      /* Date format */
#define T_FMT 3      /* Time format */
#define T_FMT_AMPM 4 /* 12-hour time format with AM/PM */
#define AM_STR 5     /* AM string */
#define PM_STR 6     /* PM string */

/* Day names (abbreviated and full) */
#define DAY_1 7  /* Sunday (full) */
#define DAY_2 8  /* Monday */
#define DAY_3 9  /* Tuesday */
#define DAY_4 10 /* Wednesday */
#define DAY_5 11 /* Thursday */
#define DAY_6 12 /* Friday */
#define DAY_7 13 /* Saturday */

#define ABDAY_1 14 /* Sun */
#define ABDAY_2 15 /* Mon */
#define ABDAY_3 16 /* Tue */
#define ABDAY_4 17 /* Wed */
#define ABDAY_5 18 /* Thu */
#define ABDAY_6 19 /* Fri */
#define ABDAY_7 20 /* Sat */

/* Month names (abbreviated and full) */
#define MON_1 21  /* January */
#define MON_2 22  /* February */
#define MON_3 23  /* March */
#define MON_4 24  /* April */
#define MON_5 25  /* May */
#define MON_6 26  /* June */
#define MON_7 27  /* July */
#define MON_8 28  /* August */
#define MON_9 29  /* September */
#define MON_10 30 /* October */
#define MON_11 31 /* November */
#define MON_12 32 /* December */

#define ABMON_1 33  /* Jan */
#define ABMON_2 34  /* Feb */
#define ABMON_3 35  /* Mar */
#define ABMON_4 36  /* Apr */
#define ABMON_5 37  /* May */
#define ABMON_6 38  /* Jun */
#define ABMON_7 39  /* Jul */
#define ABMON_8 40  /* Aug */
#define ABMON_9 41  /* Sep */
#define ABMON_10 42 /* Oct */
#define ABMON_11 43 /* Nov */
#define ABMON_12 44 /* Dec */

/* LC_TIME additional */
#define ERA 45         /* Era description (optional) */
#define ERA_D_FMT 46   /* Era date format */
#define ERA_D_T_FMT 47 /* Era date and time format */
#define ERA_T_FMT 48   /* Era time format */
#define ALT_DIGITS 49  /* Alternative symbols for digits */

/* LC_NUMERIC category */
#define RADIXCHAR 50 /* Radix character (decimal point) */
#define THOUSEP 51   /* Thousands separator */

/* LC_MONETARY category */
#define CRNCYSTR 52 /* Currency symbol with position indicator */

/* LC_MESSAGES category */
#define YESEXPR 53 /* Affirmative response regex */
#define NOEXPR 54  /* Negative response regex */
#define YESSTR 55  /* Affirmative response string (obsolete) */
#define NOSTR 56   /* Negative response string (obsolete) */

/* Additional items */
#define _DATE_FMT 57 /* strftime format for date(1) */
#define _NL_ITEM_MAX 58

    /*
     * Get locale-specific information.
     * Returns a pointer to a string for the specified item.
     * The returned string should not be modified or freed.
     */
    char *nl_langinfo(nl_item item);

/*
 * Get locale-specific information for specified locale.
 * locale_t version (POSIX.1-2008).
 */
#ifdef _POSIX_C_SOURCE
#if _POSIX_C_SOURCE >= 200809L
#include <locale.h>
    char *nl_langinfo_l(nl_item item, locale_t locale);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* _LANGINFO_H */
