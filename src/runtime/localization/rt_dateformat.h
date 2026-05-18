//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_dateformat.h
// Purpose: Public C API for Viper.Localization.DateFormat — locale-aware
//          date and time formatting via CLDR pattern letters. The bound
//          Locale's rt_locale_data_t supplies month/day name tables,
//          AM/PM tokens, and the short/medium/long/full pattern templates.
//
// Key invariants:
//   - DateTime inputs are Unix timestamps (int64 seconds since epoch), the
//     same representation used by rt_datetime_* component accessors.
//   - Custom patterns use CLDR letter conventions (y/M/d/E/H/h/m/s/a) plus
//     quoted literals. Unsupported letters trap with a clear diagnostic.
//   - MonthName / DayName methods accept a 1-based month (1-12) or a
//     0-based weekday (0=Sunday..6=Saturday) per the rest of the runtime's
//     calendar conventions.
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_dateformat.c (class methods),
//        src/runtime/localization/rt_dateformat_patterns.c (emit engine),
//        src/runtime/core/rt_datetime.h (component accessors),
//        docs/viperlib/localization/formatting.md (user documentation).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt.hpp"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

/// @brief Create a date formatter bound to the process's current locale.
void *rt_dateformat_new(void);
/// @brief Create a date formatter bound to the given @p locale handle.
void *rt_dateformat_for_locale(void *locale);

//===----------------------------------------------------------------------===//
// Properties
//===----------------------------------------------------------------------===//

/// @brief Return the Locale handle this formatter was built with (borrowed).
void *rt_dateformat_get_locale(void *self);

//===----------------------------------------------------------------------===//
// Canonical style methods (take a Unix timestamp i64)
//===----------------------------------------------------------------------===//

/// @brief Format the date of @p timestamp in the locale's short style.
rt_string rt_dateformat_short(void *self, int64_t timestamp);
/// @brief Format the date of @p timestamp in the locale's medium style.
rt_string rt_dateformat_medium(void *self, int64_t timestamp);
/// @brief Format the date of @p timestamp in the locale's long style.
rt_string rt_dateformat_long(void *self, int64_t timestamp);
/// @brief Format the date of @p timestamp in the locale's full style.
rt_string rt_dateformat_full(void *self, int64_t timestamp);

/// @brief Format the time-of-day of @p timestamp in the short style.
rt_string rt_dateformat_time_short(void *self, int64_t timestamp);
/// @brief Format the time-of-day of @p timestamp in the medium style.
rt_string rt_dateformat_time_medium(void *self, int64_t timestamp);

/// @brief Format both date and time of @p timestamp in the short style.
rt_string rt_dateformat_datetime_short(void *self, int64_t timestamp);
/// @brief Format both date and time of @p timestamp in the medium style.
rt_string rt_dateformat_datetime_medium(void *self, int64_t timestamp);

/// @brief Format @p timestamp using a custom CLDR pattern.
/// @details Supported letters: y M d E H h m s a. Quoted literals via '.
///          Unsupported letters trap. Pattern length capped at 256 bytes.
rt_string rt_dateformat_custom(void *self, int64_t timestamp, rt_string pattern);

/// @brief Format a DateOnly (@p dateonly is an rt_dateonly_t handle) using
///        a named style ("short"/"medium"/"long"/"full").
rt_string rt_dateformat_date_only(void *self, void *dateonly, rt_string style);

/// @brief Get the month name for month @p month (1-12). @p abbreviated picks
///        between wide (false) and abbreviated (true) forms.
rt_string rt_dateformat_month_name(void *self, int64_t month, int8_t abbreviated);

/// @brief Get the weekday name for @p dow (0=Sunday..6=Saturday).
rt_string rt_dateformat_day_name(void *self, int64_t dow, int8_t abbreviated);

/// @brief Get the AM/PM token for the given boolean (1 = PM, 0 = AM).
rt_string rt_dateformat_am_pm(void *self, int8_t is_pm);

#ifdef __cplusplus
}
#endif
