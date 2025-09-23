// File: src/support/symbol.cpp
// Purpose: Implements helpers for the Symbol identifier type.
// Key invariants: Symbol value 0 remains reserved for invalid identifiers.
// Ownership/Lifetime: Operates on value types without owning resources.
// Links: docs/codemap.md

#include "support/symbol.hpp"

namespace il::support
{
/// @brief Compare two symbols for equality.
/// @param a First symbol to compare.
/// @param b Second symbol to compare.
/// @return True when both symbols refer to the same identifier.
bool operator==(Symbol a, Symbol b) noexcept
{
    return a.id == b.id;
}

/// @brief Compare two symbols for inequality.
/// @param a First symbol to compare.
/// @param b Second symbol to compare.
/// @return True when the symbols refer to different identifiers.
bool operator!=(Symbol a, Symbol b) noexcept
{
    return a.id != b.id;
}

/// @brief Check whether the symbol denotes a valid interned string.
/// @return True when the identifier is non-zero.
Symbol::operator bool() const noexcept
{
    return id != 0;
}
} // namespace il::support

namespace std
{
/// @brief Compute a hash value for an interned string symbol.
/// @param s Symbol handle to hash.
/// @return Stable hash derived from the underlying identifier.
size_t hash<il::support::Symbol>::operator()(il::support::Symbol s) const noexcept
{
    return s.id;
}
} // namespace std

