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
// Links: docs/internals/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace il::support {

/// @brief Opaque identifier for interned strings.
/// @invariant 0 denotes an invalid symbol.
/// @ownership Value type, no ownership semantics.
struct Symbol {
    uint32_t id = 0; ///< 1-based index into the interner's storage; 0 is invalid.

    /// @brief Check whether the symbol is valid (non-zero id).
    /// @return true if the symbol holds a valid interned string identifier.
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return id != 0;
    }
};

/// @brief Compare two symbols for equality.
/// @param a First symbol.
/// @param b Second symbol.
/// @return true if both symbols refer to the same interned string.
[[nodiscard]] constexpr bool operator==(Symbol a, Symbol b) noexcept {
    return a.id == b.id;
}

/// @brief Compare two symbols for inequality.
/// @param a First symbol.
/// @param b Second symbol.
/// @return true if the symbols refer to different interned strings.
[[nodiscard]] constexpr bool operator!=(Symbol a, Symbol b) noexcept {
    return a.id != b.id;
}
} // namespace il::support

namespace std {
/// @brief Standard hash specialization for Symbol.
/// @details Enables Symbol to be used as a key in unordered containers.
template <> struct hash<il::support::Symbol> {
    /// @brief Compute the hash value for a symbol.
    /// @param s Symbol to hash.
    /// @return Hash value derived from the symbol's id.
    constexpr size_t operator()(il::support::Symbol s) const noexcept {
        return s.id;
    }
};
} // namespace std
