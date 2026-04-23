//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_info.c
// Purpose: Implementation of Viper.Localization.LocaleInfo. Each query pulls
//          the bound rt_locale_data_t via rt_locale_get_data (which falls back
//          to the invariant record when the Locale has no bound data) and
//          returns a fresh rt_string with the appropriate field.
//
// Key invariants:
//   - Every return path yields a non-NULL rt_string; empty strings stand in
//     for missing fields (no null-vs-empty-string ambiguity).
//   - Matches the plan's "static utility class (LocaleInfo)" shape: no
//     instance state, no construction, just pure queries.
//
// Ownership/Lifetime:
//   - Returned strings are fresh allocations owned by the caller.
//
// Links: src/runtime/localization/rt_locale_info.h (interface),
//        src/runtime/localization/rt_locale.h (Locale handle accessor).
//
//===----------------------------------------------------------------------===//

#include "rt_locale_info.h"

#include "rt_internal.h"
#include "rt_locale.h"
#include "rt_locale_data.h"
#include "rt_string.h"

#include <stdint.h>
#include <string.h>

static rt_string loc_info_str(const char *s) {
    if (!s || !*s)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(s, strlen(s));
}

rt_string rt_locale_info_display_name(void *locale, void *in_locale) {
    (void)in_locale; // Phase 1: only en-US is baked, so we always emit the
                     // native display name of the target locale.
    const rt_locale_data_t *d = rt_locale_get_data(locale);
    return loc_info_str(d->names.display);
}

rt_string rt_locale_info_language_name(void *locale, void *in_locale) {
    (void)in_locale;
    const rt_locale_data_t *d = rt_locale_get_data(locale);
    return loc_info_str(d->names.language);
}

rt_string rt_locale_info_region_name(void *locale, void *in_locale) {
    (void)in_locale;
    const rt_locale_data_t *d = rt_locale_get_data(locale);
    return loc_info_str(d->names.region);
}

rt_string rt_locale_info_text_direction(void *locale) {
    const rt_locale_data_t *d = rt_locale_get_data(locale);
    return loc_info_str(d->text_direction);
}

int64_t rt_locale_info_first_day_of_week(void *locale) {
    const rt_locale_data_t *d = rt_locale_get_data(locale);
    return (int64_t)d->first_day_of_week;
}

int8_t rt_locale_info_is_rtl(void *locale) {
    const rt_locale_data_t *d = rt_locale_get_data(locale);
    return (int8_t)(strcmp(d->text_direction, "rtl") == 0 ? 1 : 0);
}

rt_string rt_locale_info_measurement(void *locale) {
    const rt_locale_data_t *d = rt_locale_get_data(locale);
    return loc_info_str(d->measurement);
}

rt_string rt_locale_info_currency(void *locale) {
    const rt_locale_data_t *d = rt_locale_get_data(locale);
    return loc_info_str(d->currency.default_code);
}
