//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_info.h
// Purpose: Static utility surface for Viper.Localization.LocaleInfo — queries
//          about a Locale's display name, text direction, first day of week,
//          measurement system, and default currency. All queries read from
//          the Locale's bound rt_locale_data_t; NULL locales and locales
//          without bound data fall through to the invariant locale's values.
//
// Key invariants:
//   - Every method tolerates NULL locale inputs without trapping; only
//     internal corruption would cause a trap here.
//   - Display-name queries support an optional `inLocale` parameter so
//     callers can render "français" in en-US vs "French" in en-US. Phase 1
//     ignores the second parameter (we only have en-US baked) and always
//     emits the native display name.
//
// Ownership/Lifetime:
//   - Returned rt_string values are fresh allocations owned by the caller.
//
// Links: src/runtime/localization/rt_locale_info.c (implementation),
//        src/runtime/localization/rt_locale.h (Locale handle type).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt.hpp"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Combined display name for the locale (e.g. "English (United States)").
rt_string rt_locale_info_display_name(void *locale, void *in_locale);

/// @brief Native-language display name (e.g. "English", "français").
rt_string rt_locale_info_language_name(void *locale, void *in_locale);

/// @brief Native region name (e.g. "United States", "France").
rt_string rt_locale_info_region_name(void *locale, void *in_locale);

/// @brief Dominant text direction: "ltr" or "rtl".
rt_string rt_locale_info_text_direction(void *locale);

/// @brief First day of the week (0 = Sunday .. 6 = Saturday).
int64_t rt_locale_info_first_day_of_week(void *locale);

/// @brief True when the locale's script is predominantly right-to-left.
int8_t rt_locale_info_is_rtl(void *locale);

/// @brief Measurement system code: "metric", "us", or "uk".
rt_string rt_locale_info_measurement(void *locale);

/// @brief Default ISO-4217 currency code for the region (e.g. "USD").
rt_string rt_locale_info_currency(void *locale);

#ifdef __cplusplus
}
#endif
