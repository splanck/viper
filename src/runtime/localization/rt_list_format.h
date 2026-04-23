//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_list_format.h
// Purpose: Public C API for Viper.Localization.ListFormat — joins a list of
//          strings using locale-appropriate conjunction styles (And / Or /
//          Unit / Short). Pulls templates (pair/start/middle/end) from the
//          bound locale's list_format data.
//
// Key invariants:
//   - Empty list -> empty string; single-item list -> the item verbatim;
//     two items -> the pair template; three or more items -> start template
//     applied left-to-right with the end template consuming the last item.
//   - Templates use {0} and {1} positional placeholders; any other
//     placeholder syntax is emitted literally.
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_list_format.c (implementation),
//        src/runtime/localization/rt_locale_data.h (list_format tables),
//        docs/viperlib/localization/formatting.md (user documentation).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void *rt_list_format_new(void);
void *rt_list_format_for_locale(void *locale);
void *rt_list_format_get_locale(void *self);

/// @brief Join with conjunction ("A, B, and C" style).
rt_string rt_list_format_and(void *self, void *items);
/// @brief Join with disjunction ("A, B, or C").
rt_string rt_list_format_or(void *self, void *items);
/// @brief Plain unit-style join (no conjunction, locale's `unit` template).
rt_string rt_list_format_unit(void *self, void *items);
/// @brief Short form — in v1 shares And's templates; future locales may
///        provide distinct ampersand-style short patterns.
rt_string rt_list_format_short(void *self, void *items);

#ifdef __cplusplus
}
#endif
