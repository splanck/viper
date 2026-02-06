//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/runtime/RuntimeSignaturesData.hpp
// Purpose: Provides static runtime signature specifications and symbol metadata.
// Key invariants: Signature specification order matches RtSig enumerators and symbols.
// Ownership/Lifetime: Inline constexpr tables with static storage across translation units.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Static runtime signature specifications and symbol metadata.
/// @details Provides constexpr tables mapping runtime signature enumerators to
///          textual specs and symbol names. These tables are used by signature
///          parsing, runtime binding, and verification routines.

#pragma once

#include "il/runtime/RuntimeSignatures.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace il::runtime::data
{

/// @brief Number of runtime signature entries.
/// @details Mirrors the RtSig::Count enumerator to size the tables below.
inline constexpr std::size_t kRtSigCount = static_cast<std::size_t>(RtSig::Count);

/// @brief Specification strings for each runtime signature.
/// @details Indexed by RtSig enumerators in declaration order. Each entry is
///          the human-readable signature string used by the runtime.
inline constexpr std::array<std::string_view, kRtSigCount> kRtSigSpecs = {
#define SIG(name, spec) std::string_view(spec),
#include "il/runtime/RuntimeSigs.def"
#undef SIG
};

/// @brief Symbol names corresponding to each runtime signature.
/// @details Indexed by RtSig enumerators in the same order as @ref kRtSigSpecs.
///          These are the linker-visible names used by the runtime bridge.
inline constexpr std::array<std::string_view, kRtSigCount> kRtSigSymbolNames = {
    "rt_print_str",    "rt_print_i64",        "rt_print_f64",
    "rt_len",          "rt_substr",           "rt_trap",
    "rt_diag_assert",  "rt_concat",           "rt_input_line",
    "rt_split_fields", "rt_to_int",           "rt_to_double",
    "rt_parse_int64",  "rt_parse_double",     "rt_int_to_str",
    "rt_f64_to_str",   "rt_str_i16_alloc",    "rt_str_i32_alloc",
    "rt_str_f_alloc",  "rt_str_retain_maybe", "rt_str_release_maybe",
};

static_assert(kRtSigSpecs.size() == kRtSigSymbolNames.size(),
              "runtime signature tables are misaligned");

} // namespace il::runtime::data
