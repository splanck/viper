//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_reltime_format.h
// Purpose: Public C API for Viper.Localization.RelativeTimeFormat — formats
//          durations as human-readable relative-time expressions ("3 days
//          ago", "in 2 hours"). Unit selection is automatic based on the
//          duration magnitude; plural form selection routes through
//          PluralRules for the bound locale.
//
// Key invariants:
//   - Unit thresholds: >=1y -> year, >=30d -> month, >=7d -> week, >=1d -> day,
//     >=1h -> hour, >=1m -> minute, else second. Sign of duration picks past
//     vs. future template.
//   - Duration inputs are int64 (milliseconds, per rt_duration's convention).
//     Positive values render as past ("N units ago"); negative values render
//     as future ("in N units"). This matches the common "elapsed since now"
//     framing used throughout Viper game/UI code.
//
// Ownership/Lifetime:
//   - Instances are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_reltime_format.c (implementation),
//        src/runtime/localization/rt_plural_rules.h (category selection),
//        src/runtime/core/rt_duration.h (input handle semantics),
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

/// @brief Create a relative-time formatter bound to the current locale.
void *rt_reltimefmt_new(void);
/// @brief Create a relative-time formatter bound to the given @p locale.
void *rt_reltimefmt_for_locale(void *locale);

//===----------------------------------------------------------------------===//
// Properties
//===----------------------------------------------------------------------===//

/// @brief Return the Locale handle this formatter was built with (borrowed).
void *rt_reltimefmt_get_locale(void *self);

/// @brief Current style identifier ("long" default; "short" for compact form).
rt_string rt_reltimefmt_get_style(void *self);
/// @brief Set the style ("long" or "short"); other values trap.
void rt_reltimefmt_set_style(void *self, rt_string style);

//===----------------------------------------------------------------------===//
// Format methods
//===----------------------------------------------------------------------===//

/// @brief Format @p duration (ms) using the default style. Positive = past;
///        negative = future.
rt_string rt_reltimefmt_format(void *self, int64_t duration);

/// @brief Format relative time between two timestamps (then -> now).
rt_string rt_reltimefmt_format_from(void *self, int64_t then_ts, int64_t now_ts);

/// @brief Short-style format.
rt_string rt_reltimefmt_short(void *self, int64_t duration);

/// @brief Long-style format (default).
rt_string rt_reltimefmt_long(void *self, int64_t duration);

/// @brief Format @p value with an explicit @p unit (one of "second"/"minute"/
///        "hour"/"day"/"week"/"month"/"year"). Sign of @p value picks past/future.
rt_string rt_reltimefmt_numeric(void *self, int64_t value, rt_string unit);

#ifdef __cplusplus
}
#endif
