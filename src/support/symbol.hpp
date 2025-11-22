//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/symbol.hpp
// Purpose: Defines Symbol handle type for interned strings. 
// Key invariants: Value 0 denotes an invalid symbol.
// Ownership/Lifetime: Symbols are value types.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>

namespace il::support
{

/// @brief Opaque identifier for interned strings.
/// @invariant 0 denotes an invalid symbol.
/// @ownership Value type, no ownership semantics.
struct Symbol
{
    uint32_t id = 0;

    [[nodiscard]] explicit operator bool() const noexcept;
};

bool operator==(Symbol a, Symbol b) noexcept;
bool operator!=(Symbol a, Symbol b) noexcept;
} // namespace il::support

namespace std
{
template <> struct hash<il::support::Symbol>
{
    size_t operator()(il::support::Symbol s) const noexcept;
};
} // namespace std
