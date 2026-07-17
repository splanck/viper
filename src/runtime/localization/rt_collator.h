//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_collator.h
// Purpose: Public C API for Zanna.Localization.Collator — locale-aware string
//          comparison with configurable strength, case sensitivity, and
//          accent sensitivity. Uses a DUCET-lite weight table (primary /
//          secondary / tertiary) covering basic Latin, Latin-1 Supplement,
//          and Latin Extended-A diacritics. Locale tailorings for sv / de
//          are applied as override patches at collator construction time.
//
// Key invariants:
//   - Strength 1 compares primary weights only (base letters).
//   - Strength 2 adds secondary weights (accents differentiate).
//   - Strength 3 adds tertiary weights (case differentiates; default).
//   - Strength 4 is unsupported in v1; setting it warns via rt_diag and
//     clamps to 3.
//   - Input strings capped at 1 MiB per comparison; above that, trap.
//   - Unsupported scripts fall back to codepoint-order comparison (not a
//     silent failure — behavior is documented).
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_collator.c (implementation),
//        src/runtime/localization/rt_collator_table.c (weight classifier),
//        docs/zannalib/localization/collation.md (user documentation).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt.hpp"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// Constructors / properties
//===----------------------------------------------------------------------===//

/// @brief Create a collator bound to the process's current locale.
void *rt_collator_new(void);
/// @brief Create a collator bound to the given @p locale handle.
void *rt_collator_for_locale(void *locale);
/// @brief Return the Locale handle this collator was built with (borrowed).
void *rt_collator_get_locale(void *self);

/// @brief Get the comparison strength (1=primary, 2=+accent, 3=+case).
int64_t rt_collator_get_strength(void *self);
/// @brief Set the comparison strength; values >3 warn and clamp to 3.
void rt_collator_set_strength(void *self, int64_t value);

/// @brief Get whether case differences are ignored (0/1).
int8_t rt_collator_get_ignore_case(void *self);
/// @brief Set whether case differences are ignored.
void rt_collator_set_ignore_case(void *self, int8_t value);

/// @brief Get whether accent/diacritic differences are ignored (0/1).
int8_t rt_collator_get_ignore_accents(void *self);
/// @brief Set whether accent/diacritic differences are ignored.
void rt_collator_set_ignore_accents(void *self, int8_t value);

//===----------------------------------------------------------------------===//
// Comparisons
//===----------------------------------------------------------------------===//

/// @brief Returns -1 / 0 / +1 per locale-aware comparison.
int64_t rt_collator_compare(void *self, rt_string a, rt_string b);

/// @brief Convenience: 1 if @p a and @p b compare equal under this collator.
int8_t rt_collator_equals(void *self, rt_string a, rt_string b);

/// @brief Generate a deterministic byte sequence whose binary comparison
///        matches this collator's Compare. Returned as a hex-encoded string
///        so it fits inside an rt_string without NUL issues.
rt_string rt_collator_sort_key(void *self, rt_string s);

/// @brief Sort a List<str> in place-equivalent new List using this collator.
void *rt_collator_sort(void *self, void *items);

//===----------------------------------------------------------------------===//
// Internal API shared with the weight table
//===----------------------------------------------------------------------===//

typedef struct rt_collator_locale_patch {
    uint32_t codepoint;
    uint32_t primary_override;
    uint16_t secondary_override;
    uint16_t tertiary_override;
} rt_collator_locale_patch_t;

/// @brief Populate P/S/T weights for a codepoint. Returns 0 on known
///        characters; 1 when the codepoint falls into codepoint-order
///        fallback territory (caller should use cp itself as primary).
int rt_collator_codepoint_weights(uint32_t cp,
                                  uint32_t *primary,
                                  uint16_t *secondary,
                                  uint16_t *tertiary);

/// @brief Return the locale patch table for @p tag (or NULL). @p out_count
///        is set to the patch count.
const rt_collator_locale_patch_t *rt_collator_locale_patches(const char *tag, size_t *out_count);

#ifdef __cplusplus
}
#endif
