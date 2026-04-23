//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_numformat.h
// Purpose: Public C API for Viper.Localization.NumberFormat — locale-aware
//          number formatting AND parsing. Wraps a Locale with mutable per-
//          instance options (fraction digit range, grouping on/off, strict
//          mode, rounding policy). Delegates to the shared digit-grouping
//          helper in rt_numfmt_internal.h so the existing
//          Viper.Text.NumberFormat surface continues to work unchanged.
//
// Key invariants:
//   - Every instance captures the Locale's rt_locale_data_t at construction;
//     subsequent reads never touch the registry (lock-free hot path).
//   - Strict mode rejects inputs where the locale's group separator appears
//     in a position that doesn't match the locale's grouping (e.g., "1,00"
//     under en-US is ambiguous between "1.00" and "100" — rejected). Lenient
//     mode accepts the input by treating the separator as informational.
//   - Rounding modes match IEEE 754: halfEven (default / banker's), halfUp,
//     halfDown, up, down, ceiling, floor.
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_numformat.c (implementation),
//        src/runtime/text/rt_numfmt_internal.h (shared grouping helper),
//        src/runtime/localization/rt_locale.h (Locale handle type),
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

/// @brief Default constructor; uses the current LocaleManager locale.
void *rt_numformat_new(void);

/// @brief Construct a NumberFormat bound to @p locale.
void *rt_numformat_for_locale(void *locale);

//===----------------------------------------------------------------------===//
// Properties
//===----------------------------------------------------------------------===//

void *rt_numformat_get_locale(void *self);
int64_t rt_numformat_get_min_frac(void *self);
void    rt_numformat_set_min_frac(void *self, int64_t value);
int64_t rt_numformat_get_max_frac(void *self);
void    rt_numformat_set_max_frac(void *self, int64_t value);
int8_t  rt_numformat_get_grouping(void *self);
void    rt_numformat_set_grouping(void *self, int8_t value);
int8_t  rt_numformat_get_strict(void *self);
void    rt_numformat_set_strict(void *self, int8_t value);
rt_string rt_numformat_get_rounding(void *self);
void      rt_numformat_set_rounding(void *self, rt_string mode);

//===----------------------------------------------------------------------===//
// Format methods
//===----------------------------------------------------------------------===//

rt_string rt_numformat_decimal(void *self, double value);
rt_string rt_numformat_decimal_n(void *self, double value, int64_t digits);
rt_string rt_numformat_integer(void *self, int64_t value);
rt_string rt_numformat_percent(void *self, double value);
rt_string rt_numformat_currency(void *self, double value);
rt_string rt_numformat_currency_of(void *self, double value, rt_string code);
rt_string rt_numformat_scientific(void *self, double value, int64_t digits);
rt_string rt_numformat_ordinal(void *self, int64_t value);

//===----------------------------------------------------------------------===//
// Parse methods
//===----------------------------------------------------------------------===//

/// @brief Strict-or-lenient parse of a locale-formatted decimal string.
/// @details Traps on invalid input; use the Try* variant for null-on-failure.
double rt_numformat_parse_decimal(void *self, rt_string input);

/// @brief Try-parse variant: returns Option<f64> (Some on success, None on failure).
void *rt_numformat_try_parse_decimal(void *self, rt_string input);

/// @brief Integer parse; rejects fractional content.
int64_t rt_numformat_parse_integer(void *self, rt_string input);
void   *rt_numformat_try_parse_integer(void *self, rt_string input);

/// @brief Currency parse; accepts an optional leading / trailing currency
///        symbol (either the locale default or an arbitrary symbol).
double rt_numformat_parse_currency(void *self, rt_string input);
void  *rt_numformat_try_parse_currency(void *self, rt_string input);

#ifdef __cplusplus
}
#endif
