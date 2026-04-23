//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_text_direction.h
// Purpose: Public C API for Viper.Localization.TextDirection — a static
//          utility class that classifies text as left-to-right (LTR),
//          right-to-left (RTL), or mixed, based on UTF-8 codepoint scanning
//          against a fixed RTL-script range table.
//
// Key invariants:
//   - Codepoint classification: characters in Hebrew / Arabic / Syriac /
//     Thaana / N'Ko ranges are strong-RTL; Latin / Greek / Cyrillic / CJK
//     / etc. are strong-LTR; digits / punctuation / whitespace are neutral.
//   - IsRTL is true only when a dominant RTL signal is present (first strong
//     codepoint is RTL, or the majority of strong codepoints are RTL).
//   - Bidi(str) does NOT implement the full Unicode BiDi algorithm; it only
//     wraps RTL runs with U+202E (RLO) / U+202C (PDF) marks so mixed-script
//     strings render with deterministic direction. Full UBA is out of scope.
//
// Ownership/Lifetime:
//   - Static class; no instance state. All functions take explicit input
//     parameters and return fresh strings.
//
// Links: src/runtime/localization/rt_text_direction.c (implementation),
//        docs/viperlib/localization/collation.md (user documentation).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt.hpp"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Return the text direction declared by @p locale's data ("ltr"/"rtl").
rt_string rt_text_direction_of_locale(void *locale);

/// @brief Detect text direction of @p s by scanning codepoints.
/// @return "ltr" / "rtl" / "mixed" / "" (empty input).
rt_string rt_text_direction_detect(rt_string s);

/// @brief True when the majority of strong codepoints in @p s are RTL.
int8_t rt_text_direction_is_rtl(rt_string s);

/// @brief True when the majority of strong codepoints in @p s are LTR.
int8_t rt_text_direction_is_ltr(rt_string s);

/// @brief Return "ltr"/"rtl"/"neutral" based on the first strong codepoint.
rt_string rt_text_direction_first_strong(rt_string s);

/// @brief Wrap mixed-script @p s with BiDi override marks when it contains
///        both LTR and RTL content. Pure-LTR and pure-RTL inputs pass
///        through unchanged.
rt_string rt_text_direction_bidi(rt_string s);

#ifdef __cplusplus
}
#endif
