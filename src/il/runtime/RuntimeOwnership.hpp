//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeOwnership.hpp
// Purpose: Centralize runtime string ownership metadata for optimizer queries.
// Key invariants: Unknown helpers are classified conservatively with no
//                 ownership facts. Bitmasks refer to explicit IL-visible
//                 arguments, not hidden bridge parameters.
// Ownership/Lifetime: Header-only table; no dynamic storage.
// Links: docs/il-passes.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

namespace il::runtime {

/// @brief Ownership effects attached to a runtime helper call.
struct RuntimeOwnershipEffects {
    std::uint64_t consumedArgMask{0}; ///< Arguments whose ownership is consumed.
    std::uint64_t retainedArgMask{0}; ///< Arguments whose reference count is retained.
    bool returnsOwned{false};         ///< Result is an owned string/reference handle.
    bool mayAllocate{false};          ///< Helper may allocate runtime-managed storage.

    /// @brief Query whether the helper consumes argument @p index.
    [[nodiscard]] constexpr bool consumesArg(unsigned index) const noexcept {
        return index < 64 && (consumedArgMask & (std::uint64_t{1} << index)) != 0;
    }

    /// @brief Query whether the helper retains argument @p index.
    [[nodiscard]] constexpr bool retainsArg(unsigned index) const noexcept {
        return index < 64 && (retainedArgMask & (std::uint64_t{1} << index)) != 0;
    }

    /// @brief True when any ownership fact is known.
    [[nodiscard]] constexpr bool hasAny() const noexcept {
        return consumedArgMask != 0 || retainedArgMask != 0 || returnsOwned || mayAllocate;
    }
};

/// @brief Classify string/reference ownership effects for known runtime helpers.
/// @details This metadata prevents optimizers from treating owned string
///          construction and consuming calls as pure value operations. Names
///          include both C runtime symbols and high-level runtime namespace
///          aliases produced by frontends.
[[nodiscard]] inline RuntimeOwnershipEffects classifyRuntimeOwnership(std::string_view name) {
    RuntimeOwnershipEffects effects{};

    if (name == "rt_str_concat" || name == "Viper.String.Concat") {
        effects.consumedArgMask = 0b11;
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_str_release" || name == "rt_str_release_maybe" ||
        name == "Viper.String.ReleaseMaybe") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_str_retain" || name == "rt_str_retain_maybe" ||
        name == "Viper.String.RetainMaybe") {
        effects.retainedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_str_empty") {
        effects.returnsOwned = true;
        return effects;
    }

    if (name == "rt_str_substr" || name == "rt_csv_quote_alloc" ||
        name == "rt_str_split_fields" || name == "rt_int_to_str" ||
        name == "rt_f64_to_str" || name == "rt_str_i16_alloc" ||
        name == "rt_str_i32_alloc" || name == "rt_str_f_alloc" ||
        name == "rt_const_cstr" || name == "rt_str_from_lit" ||
        name == "rt_str_left" || name == "rt_str_right" || name == "rt_str_mid" ||
        name == "rt_str_mid_len" || name == "rt_str_ltrim" ||
        name == "rt_str_rtrim" || name == "rt_str_trim" ||
        name == "rt_str_ucase" || name == "rt_str_lcase" || name == "rt_str_chr" ||
        name == "rt_args_get" || name == "rt_cmdline" || name == "rt_getkey_str" ||
        name == "rt_inkey_str" || name == "rt_term_read_line" ||
        name == "rt_term_ask" || name == "Viper.String.Left" ||
        name == "Viper.String.Right" || name == "Viper.String.Mid2" ||
        name == "Viper.String.Mid3" || name == "Viper.String.LTrim" ||
        name == "Viper.String.RTrim" || name == "Viper.String.Trim" ||
        name == "Viper.String.UCase" || name == "Viper.String.LCase" ||
        name == "Viper.String.Chr" || name == "Viper.String.FromI64" ||
        name == "Viper.String.FromF64") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    return effects;
}

} // namespace il::runtime
