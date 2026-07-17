//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_message_bundle.h
// Purpose: Public C API for Zanna.Localization.MessageBundle — a translation
//          catalog keyed by message ID. Supports placeholder interpolation
//          ({name} and positional {0}/{1}/…), fallback chains (bundles stack
//          so missing keys cascade up), and CLDR plural selection via
//          PluralRules.
//
// Key invariants:
//   - Keys and values are rt_strings. NUL bytes in keys are not supported.
//   - Fallback chains cannot cycle: SetFallback traps when it detects a
//     self-cycle; depth capped at 16.
//   - Get traps when no key in the chain resolves; TryGet returns empty
//     string without trapping.
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated; GC-managed.
//   - Entries are retained by the bundle's internal rt_map.
//
// Links: src/runtime/localization/rt_message_bundle.c (implementation),
//        src/runtime/localization/rt_plural_rules.h (category evaluator),
//        docs/zannalib/localization/messages.md (user documentation).
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

/// @brief Default constructor — empty bundle bound to the current locale.
void *rt_message_bundle_new(void);

/// @brief Load a translation map from a JSON file at @p path.
/// @details Expects a flat object: {"key": "value", ...}. Non-string values
///          are rejected. Traps on file/JSON errors.
void *rt_message_bundle_load_from_json(void *locale, rt_string path);

/// @brief Load a translation map from a ZPAK-embedded asset by @p name.
void *rt_message_bundle_load_from_asset(void *locale, rt_string name);

/// @brief Construct from a preexisting rt_map<rt_string, rt_string>.
void *rt_message_bundle_from_map(void *locale, void *map);

//===----------------------------------------------------------------------===//
// Property accessors
//===----------------------------------------------------------------------===//

/// @brief Return the Locale handle this bundle was built with (borrowed).
void *rt_message_bundle_get_locale(void *self);
/// @brief Number of entries in this bundle's own map (excludes fallbacks).
int64_t rt_message_bundle_get_count(void *self);

//===----------------------------------------------------------------------===//
// Lookup / format
//===----------------------------------------------------------------------===//

/// @brief Resolve @p key through this bundle and its fallback chain.
/// @details Traps when no bundle in the chain has the key.
rt_string rt_message_bundle_get(void *self, rt_string key);

/// @brief Non-trapping variant: returns empty string when unresolved.
rt_string rt_message_bundle_try_get(void *self, rt_string key);

/// @brief Resolve @p key or return a caller-provided default string.
/// @details The returned string is always retained for the caller: a resolved
///          bundle value is returned from the lookup chain, while
///          @p default_value is retained before returning when the key is
///          missing. A NULL default produces an allocated empty string.
rt_string rt_message_bundle_get_or(void *self, rt_string key, rt_string default_value);

/// @brief Resolve @p key as an Option.
/// @details Returns `Some(str)` when the key is present, including when the
///          translation is the empty string, and `None` when the key cannot be
///          resolved through the bundle or fallback chain.
void *rt_message_bundle_try_get_option(void *self, rt_string key);

/// @brief Check whether @p key resolves in this bundle or its chain.
int8_t rt_message_bundle_has(void *self, rt_string key);

/// @brief Interpolate a message template with named placeholders.
/// @details Placeholder syntax: `{name}`. Uses the resolved template string
///          via Get (so fallback chain applies). Missing placeholders in
///          @p vars are preserved literally.
rt_string rt_message_bundle_format(void *self, rt_string key, void *vars);

/// @brief Positional interpolation; `{0}`, `{1}`, … index into @p values.
rt_string rt_message_bundle_format_with(void *self, rt_string key, void *values);

/// @brief Plural-aware lookup and substitution.
/// @details Evaluates the bundle's locale's cardinal plural category for
///          @p n, then looks up "<key>.<category>" (falling back to
///          "<key>.other"). Substitutes `{n}` with the numeric value using a
///          temporary variable map; @p vars is not mutated.
rt_string rt_message_bundle_plural(void *self, rt_string key, int64_t n, void *vars);

/// @brief Set the fallback bundle (returns self for chaining).
/// @details Traps on self-cycle. The fallback is retained until replaced or
///          the bundle is finalized.
void *rt_message_bundle_set_fallback(void *self, void *fallback);

/// @brief Enumerate the keys defined in this bundle (excludes fallback).
void *rt_message_bundle_keys(void *self);

#ifdef __cplusplus
}
#endif
