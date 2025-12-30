/*
 * ViperOS C Library - langinfo implementation
 * Provides locale-specific information strings
 * Currently uses "C"/"POSIX" locale values
 */

#include <langinfo.h>
#include <locale.h>

/* Static strings for the "C" locale */
static const char *const langinfo_strings[] = {
    /* CODESET */       "UTF-8",

    /* D_T_FMT */       "%a %b %e %H:%M:%S %Y",
    /* D_FMT */         "%m/%d/%y",
    /* T_FMT */         "%H:%M:%S",
    /* T_FMT_AMPM */    "%I:%M:%S %p",
    /* AM_STR */        "AM",
    /* PM_STR */        "PM",

    /* DAY_1 - DAY_7 */
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday",

    /* ABDAY_1 - ABDAY_7 */
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",

    /* MON_1 - MON_12 */
    "January", "February", "March", "April",
    "May", "June", "July", "August",
    "September", "October", "November", "December",

    /* ABMON_1 - ABMON_12 */
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",

    /* ERA */           "",
    /* ERA_D_FMT */     "",
    /* ERA_D_T_FMT */   "",
    /* ERA_T_FMT */     "",
    /* ALT_DIGITS */    "",

    /* RADIXCHAR */     ".",
    /* THOUSEP */       "",

    /* CRNCYSTR */      "",

    /* YESEXPR */       "^[yY]",
    /* NOEXPR */        "^[nN]",
    /* YESSTR */        "yes",
    /* NOSTR */         "no",

    /* _DATE_FMT */     "%a %b %e %H:%M:%S %Z %Y",
};

char *nl_langinfo(nl_item item) {
    if (item < 0 || item >= _NL_ITEM_MAX) {
        return (char *)"";
    }

    return (char *)langinfo_strings[item];
}

#ifdef _POSIX_C_SOURCE
#if _POSIX_C_SOURCE >= 200809L
char *nl_langinfo_l(nl_item item, locale_t locale) {
    (void)locale;  /* We only support C locale */
    return nl_langinfo(item);
}
#endif
#endif
