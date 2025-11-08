//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/HelperEffects.hpp
// Purpose: Provide shared classification of runtime helper side-effect flags.
// Key invariants: Effect tables remain aligned with runtime helper semantics and
//                 are reused across debug registries and runtime descriptor
//                 builders to ensure consistent optimisation metadata.
// Ownership/Lifetime: Header-only utilities containing constexpr tables.
// Links: docs/il-guide.md#reference, docs/architecture.md#runtime-signatures
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <string_view>

namespace il::runtime
{

/// @brief Describe behavioural flags associated with a runtime helper.
struct HelperEffects
{
    bool nothrow = false; ///< Helper cannot throw or trap under defined behaviour.
    bool readonly = false; ///< Helper may read memory but performs no writes.
    bool pure = false;     ///< Helper has no observable side effects.
};

/// @brief Lookup helper side-effect metadata by symbol name.
/// @param name Runtime helper symbol (e.g., "rt_len").
/// @return Effect classification; default-initialised when unknown.
inline HelperEffects classifyHelperEffects(std::string_view name)
{
    struct Entry
    {
        std::string_view name;
        HelperEffects effects;
    };

    constexpr std::array<Entry, 14> kEntries{{
        Entry{"rt_cdbl_from_any", HelperEffects{true, false, true}},
        Entry{"rt_int_floor", HelperEffects{true, false, true}},
        Entry{"rt_fix_trunc", HelperEffects{true, false, true}},
        Entry{"rt_round_even", HelperEffects{true, false, true}},
        Entry{"rt_sqrt", HelperEffects{true, false, true}},
        Entry{"rt_abs_f64", HelperEffects{true, false, true}},
        Entry{"rt_floor", HelperEffects{true, false, true}},
        Entry{"rt_ceil", HelperEffects{true, false, true}},
        Entry{"rt_sin", HelperEffects{true, false, true}},
        Entry{"rt_cos", HelperEffects{true, false, true}},
        Entry{"rt_len", HelperEffects{true, true, false}},
        Entry{"rt_instr2", HelperEffects{true, true, false}},
        Entry{"rt_instr3", HelperEffects{true, true, false}},
        Entry{"rt_str_eq", HelperEffects{true, true, false}},
    }};

    for (const auto &entry : kEntries)
        if (entry.name == name)
            return entry.effects;
    return {};
}

} // namespace il::runtime

