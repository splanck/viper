//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements helpers for the Symbol handle returned by the string interner.
// Symbols wrap a 32-bit identifier where zero represents an invalid handle.
// The utilities defined here provide comparisons and hashing used throughout
// the support library.
//
//===----------------------------------------------------------------------===//

#include "support/symbol.hpp"

namespace il::support
{
/// @brief Compare two symbols for equality.
///
/// Symbols compare by their underlying integer identifier, which uniquely maps
/// to an interned string.  This operation enables value semantics for handles.
///
/// @param a First symbol to compare.
/// @param b Second symbol to compare.
/// @return True when both symbols refer to the same identifier.
bool operator==(Symbol a, Symbol b) noexcept
{
    return a.id == b.id;
}

/// @brief Compare two symbols for inequality.
///
/// Delegates to operator== to maintain consistency between the relational
/// helpers.
///
/// @param a First symbol to compare.
/// @param b Second symbol to compare.
/// @return True when the symbols refer to different identifiers.
bool operator!=(Symbol a, Symbol b) noexcept
{
    return a.id != b.id;
}

/// @brief Check whether the symbol denotes a valid interned string.
///
/// Interned strings are assigned identifiers starting at one.  The reserved
/// identifier zero is used as a sentinel for "not found" and propagates as an
/// invalid handle.
///
/// @return True when the identifier is non-zero.
Symbol::operator bool() const noexcept
{
    return id != 0;
}
} // namespace il::support

namespace std
{
/// @brief Compute a hash value for an interned string symbol.
///
/// The hash simply returns the underlying numeric identifier because symbols
/// already provide a dense, stable mapping from strings to integers.
///
/// @param s Symbol handle to hash.
/// @return Stable hash derived from the underlying identifier.
size_t hash<il::support::Symbol>::operator()(il::support::Symbol s) const noexcept
{
    return s.id;
}
} // namespace std

