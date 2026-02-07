//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
    bool nothrow = false;  ///< Helper cannot throw or trap under defined behaviour.
    bool readonly = false; ///< Helper may read memory but performs no writes.
    bool pure = false;     ///< Helper has no observable side effects.
};

/// @brief Lookup helper side-effect metadata by symbol name.
/// @param name Runtime helper symbol (e.g., "rt_str_len").
/// @return Effect classification; default-initialised when unknown.
/// @details This table provides fast constexpr lookup for common runtime helpers.
///          For comprehensive metadata, also consult the runtime signature registry.
///          Effects are: {nothrow, readonly, pure}.
///          - pure: No observable side effects; can eliminate if result unused
///          - readonly: May read memory but no writes; can reorder with stores
///          - nothrow: Cannot throw or trap; can hoist across exception boundaries
inline HelperEffects classifyHelperEffects(std::string_view name)
{
    struct Entry
    {
        std::string_view name;
        HelperEffects effects;
    };

    // Pure math helpers: nothrow=true, readonly=false, pure=true
    // These perform pure computation with no memory access.
    constexpr std::array<Entry, 32> kEntries{{
        // Math: pure computation, no memory access
        Entry{"rt_cdbl_from_any", HelperEffects{true, false, true}},
        Entry{"rt_int_floor", HelperEffects{true, false, true}},
        Entry{"rt_fix_trunc", HelperEffects{true, false, true}},
        Entry{"rt_round_even", HelperEffects{true, false, true}},
        Entry{"rt_sqrt", HelperEffects{true, false, true}},
        Entry{"rt_abs_f64", HelperEffects{true, false, true}},
        Entry{"rt_abs_i64", HelperEffects{true, false, true}},
        Entry{"rt_floor", HelperEffects{true, false, true}},
        Entry{"rt_ceil", HelperEffects{true, false, true}},
        Entry{"rt_sin", HelperEffects{true, false, true}},
        Entry{"rt_cos", HelperEffects{true, false, true}},
        Entry{"rt_tan", HelperEffects{true, false, true}},
        Entry{"rt_atan", HelperEffects{true, false, true}},
        Entry{"rt_exp", HelperEffects{true, false, true}},
        Entry{"rt_log", HelperEffects{true, false, true}},
        Entry{"rt_sgn_i64", HelperEffects{true, false, true}},
        Entry{"rt_sgn_f64", HelperEffects{true, false, true}},

        // String inspection: readonly (reads string memory), not pure
        // These read string contents but don't modify anything.
        Entry{"rt_str_len", HelperEffects{true, true, false}},
        Entry{"rt_str_index_of", HelperEffects{true, true, false}},
        Entry{"rt_instr3", HelperEffects{true, true, false}},
        Entry{"rt_str_eq", HelperEffects{true, true, false}},
        Entry{"rt_str_lt", HelperEffects{true, true, false}},
        Entry{"rt_str_le", HelperEffects{true, true, false}},
        Entry{"rt_str_gt", HelperEffects{true, true, false}},
        Entry{"rt_str_ge", HelperEffects{true, true, false}},
        Entry{"rt_str_asc", HelperEffects{true, true, false}},

        // Array length queries: readonly (reads array header)
        Entry{"rt_arr_i32_len", HelperEffects{true, true, false}},
        Entry{"rt_arr_str_len", HelperEffects{true, true, false}},

        // Conversion helpers: nothrow only (may allocate, not pure)
        Entry{"rt_str_chr", HelperEffects{true, false, false}},
        Entry{"rt_to_int", HelperEffects{true, false, false}},
        Entry{"rt_to_double", HelperEffects{true, false, false}},
        Entry{"rt_val", HelperEffects{true, false, false}},
    }};

    for (const auto &entry : kEntries)
        if (entry.name == name)
            return entry.effects;
    return {};
}

} // namespace il::runtime
