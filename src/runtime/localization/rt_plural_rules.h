//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_plural_rules.h
// Purpose: Public C API for Zanna.Localization.PluralRules — CLDR-style
//          plural-category selection for cardinal and ordinal numeric forms.
//          Each instance is keyed on a Locale whose rt_locale_data_t carries
//          the rule AST chains populated at locale-load time (baked into
//          en-US in Phase 2; JSON-loaded in later phases).
//
// Key invariants:
//   - A PluralRules instance holds an opaque Locale handle reference
//     (strong, via rt_heap_retain) plus a captured non-owning pointer to
//     the locale's rt_locale_data_t. Unregistering the locale while a live
//     PluralRules holds its data triggers the unload-in-use trap in
//     LocaleManager's bookkeeping.
//   - Category lookup walks the rule chain in array order; the first
//     matching rule wins. Every chain terminates in an RT_PRN_TRUE node so
//     the fallback ("other") is always reachable.
//   - Rule evaluation is pure and thread-safe: no shared mutable state.
//
// Ownership/Lifetime:
//   - Instances are heap-allocated via rt_obj_new_i64; GC-managed.
//
// Links: src/runtime/localization/rt_plural_rules.c (implementation),
//        src/runtime/localization/rt_locale_data.h (rule AST storage),
//        docs/zannalib/localization/messages.md (end-user doc).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt.hpp"
#include "rt_locale_data.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// Public class surface
//===----------------------------------------------------------------------===//

/// @brief Construct PluralRules for the given Locale.
/// @details Captures the locale's data pointer; subsequent category queries
///          touch only the captured record. NULL locale => invariant rules.
void *rt_plural_rules_for_locale(void *locale);

/// @brief Select the cardinal category for a real-valued input.
/// @details Computes CLDR operand variables (n, i, v, f, t) from @p n and
///          evaluates the locale's cardinal rule chain. Returns a freshly
///          allocated string from the category set {"zero", "one", "two",
///          "few", "many", "other"}.
rt_string rt_plural_rules_cardinal(void *self, double n);

/// @brief Select the cardinal category for an integer input.
rt_string rt_plural_rules_cardinal_int(void *self, int64_t n);

/// @brief Select the ordinal category for an integer input.
/// @details Ordinal rules depend on positional suffix conventions (1st,
///          2nd, 3rd, 4th in English). Rule AST uses the `n` variable
///          (absolute value) to model these.
rt_string rt_plural_rules_ordinal(void *self, int64_t n);

/// @brief Return the full set of distinct categories used by this locale.
/// @details Equivalent to `{ category : rule in (cardinal ∪ ordinal) }`
///          with stable insertion-order preservation.
void *rt_plural_rules_categories(void *self); // List<str>

//===----------------------------------------------------------------------===//
// Internal helpers (shared with RelativeTimeFormat, MessageBundle in later
// phases; also used by RTPluralRulesTests to bypass the class construction).
//===----------------------------------------------------------------------===//

/// @brief Evaluate the cardinal chain on a rt_locale_data_t directly.
rt_plural_category_t rt_plural_rules_select_cardinal(const rt_locale_data_t *data, double n);

/// @brief Evaluate the cardinal chain with pure-integer operands.
rt_plural_category_t rt_plural_rules_select_cardinal_int(const rt_locale_data_t *data, int64_t n);

/// @brief Evaluate the ordinal chain on a rt_locale_data_t.
rt_plural_category_t rt_plural_rules_select_ordinal(const rt_locale_data_t *data, int64_t n);

/// @brief Convert a category enum to its canonical string name.
const char *rt_plural_rules_category_name(rt_plural_category_t cat);

#ifdef __cplusplus
}
#endif
