//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeOwnership.hpp
// Purpose: Centralize runtime reference ownership metadata for optimizer queries.
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
    std::uint64_t ownedOutArgMask{0}; ///< Pointer args that receive an owned reference.
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

    /// @brief Query whether pointer argument @p index receives an owned reference.
    [[nodiscard]] constexpr bool writesOwnedOutArg(unsigned index) const noexcept {
        return index < 64 && (ownedOutArgMask & (std::uint64_t{1} << index)) != 0;
    }

    /// @brief True when any ownership fact is known.
    [[nodiscard]] constexpr bool hasAny() const noexcept {
        return consumedArgMask != 0 || retainedArgMask != 0 || ownedOutArgMask != 0 ||
               returnsOwned || mayAllocate;
    }
};

namespace detail {

[[nodiscard]] constexpr bool startsWith(std::string_view value,
                                        std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] constexpr bool endsWith(std::string_view value, std::string_view suffix) noexcept {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size(), suffix.size()) == suffix;
}

[[nodiscard]] constexpr bool contains(std::string_view value, std::string_view needle) noexcept {
    return value.find(needle) != std::string_view::npos;
}

} // namespace detail

/// @brief Classify string/reference ownership effects for known runtime helpers.
/// @details This metadata prevents optimizers from treating owned string,
///          object, array, and collection construction/consumption as ordinary
///          value operations. Names include both C runtime symbols and
///          high-level runtime namespace aliases produced by frontends.
[[nodiscard]] inline RuntimeOwnershipEffects classifyRuntimeOwnership(std::string_view name) {
    RuntimeOwnershipEffects effects{};

    if (name == "rt_str_concat" || name == "Viper.String.Concat") {
        effects.consumedArgMask = 0b11;
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_str_release" || name == "rt_str_release_maybe" ||
        name == "rt_memory_release_str" || name == "Viper.String.ReleaseMaybe" ||
        name == "Viper.Memory.ReleaseStr") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_str_retain" || name == "rt_str_retain_maybe" ||
        name == "rt_memory_retain_str" || name == "Viper.String.RetainMaybe" ||
        name == "Viper.Memory.RetainStr") {
        effects.retainedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_memory_release" || name == "Viper.Memory.Release") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_memory_retain" || name == "Viper.Memory.Retain") {
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

    if (name == "rt_arr_i32_new" || name == "rt_arr_i64_new" || name == "rt_arr_f64_new" ||
        name == "rt_arr_str_alloc" || name == "rt_arr_obj_new") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_arr_i32_retain" || name == "rt_arr_i64_retain" ||
        name == "rt_arr_f64_retain") {
        effects.retainedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_arr_i32_release" || name == "rt_arr_i64_release" ||
        name == "rt_arr_f64_release" || name == "rt_arr_str_release" ||
        name == "rt_arr_obj_release") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_arr_str_get" || name == "rt_arr_obj_get") {
        effects.returnsOwned = true;
        return effects;
    }

    if (name == "rt_arr_str_put" || name == "rt_arr_obj_put") {
        effects.retainedArgMask = 0b100;
        return effects;
    }

    if (name == "rt_obj_new_i64" || name == "rt_box_i64" || name == "rt_box_f64" ||
        name == "rt_box_i1" || name == "rt_box_i1_bool" || name == "rt_box_value_type" ||
        name == "Viper.Core.Box.I64" || name == "Viper.Core.Box.F64" ||
        name == "Viper.Core.Box.I1" || name == "Viper.Core.Box.ValueType") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_box_str" || name == "Viper.Core.Box.Str") {
        effects.retainedArgMask = 0b1;
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_box_try_to_str" || name == "Viper.Core.Box.TryToStr") {
        effects.ownedOutArgMask = 0b10;
        return effects;
    }

    if (name == "rt_unbox_str" || name == "Viper.Core.Box.ToStr" ||
        name == "rt_obj_to_string" || name == "Viper.Core.Object.ToString" ||
        name == "rt_obj_type_name" || name == "Viper.Core.Object.TypeName" ||
        name == "Viper.Core.Object.get_TypeName" || name == "rt_parse_double_option" ||
        name == "rt_parse_int64_option" || name == "Viper.Core.Parse.DoubleOption" ||
        name == "Viper.Core.Parse.Int64Option" || name == "Viper.Parse.DoubleOption" ||
        name == "Viper.Parse.Int64Option" || name == "Viper.Core.Convert.ToString_Int" ||
        name == "Viper.Core.Convert.ToString_Double" || name == "Viper.Convert.ToString_Int" ||
        name == "Viper.Convert.ToString_Double") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_msgbus_new" || name == "Viper.Core.MessageBus.New" ||
        name == "rt_msgbus_callback_new" || name == "Viper.Core.MessageBus.Callback" ||
        name == "rt_msgbus_topics" || name == "Viper.Core.MessageBus.Topics") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_msgbus_subscribe" || name == "Viper.Core.MessageBus.Subscribe") {
        effects.retainedArgMask = 0b110;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_obj_retain_maybe" || name == "rt_obj_retain_known") {
        effects.retainedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_obj_release_check0" || name == "rt_obj_release_known_check0") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (detail::startsWith(name, "rt_") &&
        (detail::endsWith(name, "_new") || detail::endsWith(name, "_clone") ||
         detail::contains(name, "_from_"))) {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (detail::startsWith(name, "Viper.") &&
        (detail::endsWith(name, ".New") || detail::endsWith(name, ".Clone") ||
         detail::contains(name, ".From"))) {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    return effects;
}

} // namespace il::runtime
