//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_numfmt_internal.h
// Purpose: Internal hooks into rt_numfmt's digit-grouping pipeline, exposed so
//          the localization module's NumberFormat can emit locale-specific
//          grouping without duplicating the group-every-N-digits logic. The
//          same helper is used by Viper.Text.InvariantNumberFormat.Thousands and by
//          Viper.Localization.NumberFormat.{Decimal, Integer, Currency, ...}.
//
// Key invariants:
//   - Implementation-only: must not be included from public-facing headers.
//   - Helper operates on a pre-built decimal digit buffer; sign handling is
//     the caller's responsibility.
//   - Traps via rt_trap on string-builder allocation failure (same contract
//     as the parent Viper.Text.InvariantNumberFormat functions).
//
// Ownership/Lifetime:
//   - Caller owns the string builder and the digit buffer; the helper only
//     appends bytes.
//
// Links: src/runtime/text/rt_numfmt.c (canonical implementation host),
//        src/runtime/localization/rt_numformat.c (primary client).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string_builder.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Append @p digits into @p sb with @p sep inserted every @p group_size
///        digits from the right.
/// @details Uses no heap of its own; traps via rt_trap() when the string
///          builder reports allocation failure. @p digits must contain
///          exactly @p dlen ASCII decimal characters — callers render the
///          integer magnitude into a fixed buffer via snprintf("%llu") first.
/// @param sb          Destination string builder (non-null).
/// @param digits      Pointer to the digit bytes (non-null).
/// @param dlen        Number of digit bytes (>= 1).
/// @param sep         Group separator bytes; NULL or zero-length means no
///                    grouping (digits emitted as-is).
/// @param sep_len     Length of @p sep in bytes.
/// @param group_size  Digits per group from the right (typically 3).
void rt_numfmt_group_digits(rt_string_builder *sb,
                            const char *digits,
                            int dlen,
                            const char *sep,
                            size_t sep_len,
                            int group_size);

#ifdef __cplusplus
}
#endif
