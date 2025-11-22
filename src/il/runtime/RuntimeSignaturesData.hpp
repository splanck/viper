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

#pragma once

#include "il/runtime/RuntimeSignatures.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace il::runtime::data
{

inline constexpr std::size_t kRtSigCount = static_cast<std::size_t>(RtSig::Count);

inline constexpr std::array<std::string_view, kRtSigCount> kRtSigSpecs = {
#define SIG(name, spec) std::string_view(spec),
#include "il/runtime/RuntimeSigs.def"
#undef SIG
};

inline constexpr std::array<std::string_view, kRtSigCount> kRtSigSymbolNames = {
    "rt_print_str",     "rt_print_i64",        "rt_print_f64",
    "rt_len",           "rt_substr",           "rt_trap",
    "rt_concat",        "rt_input_line",       "rt_split_fields",
    "rt_to_int",        "rt_to_double",        "rt_parse_int64",
    "rt_parse_double",  "rt_int_to_str",       "rt_f64_to_str",
    "rt_str_i16_alloc", "rt_str_i32_alloc",    "rt_str_f_alloc",
    "rt_str_d_alloc",   "rt_str_retain_maybe", "rt_str_release_maybe",
};

static_assert(kRtSigSpecs.size() == kRtSigSymbolNames.size(),
              "runtime signature tables are misaligned");

} // namespace il::runtime::data
