//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/runtime/RuntimeNameMap.hpp
// Purpose: Central mapping from canonical Viper.* runtime symbols to C rt_* symbols.
// Key invariants: Every canonical entry maps to exactly one C symbol; lookups
//                 return std::nullopt when no mapping exists.
// Ownership/Lifetime: Header-only constexpr data; no runtime allocations.
// Links: RuntimeNameMap.inc
//
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string_view>

namespace il::runtime
{

struct RuntimeNameAlias
{
    std::string_view canonical;
    std::string_view runtime;
};

/// @brief Static map of Viper.* names to rt_* symbols used by native backends.
inline constexpr RuntimeNameAlias kRuntimeNameAliases[] = {
#define RUNTIME_NAME_ALIAS(canon, impl) RuntimeNameAlias{canon, impl},
#include "il/runtime/RuntimeNameMap.inc"
#undef RUNTIME_NAME_ALIAS
};

/// @brief Resolve a canonical Viper.* runtime name to the C runtime symbol.
/// @return Mapped symbol when present; std::nullopt otherwise.
inline constexpr std::optional<std::string_view> mapCanonicalRuntimeName(std::string_view name)
{
    for (const auto &alias : kRuntimeNameAliases)
    {
        if (alias.canonical == name)
            return alias.runtime;
    }
    return std::nullopt;
}

} // namespace il::runtime
