//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

/// @file
/// @brief Defines relational and hashing support for `il::support::Symbol`.
/// @details Symbols act as lightweight handles to interned strings.  This file
///          centralises the equality, inequality, truthiness, and hashing
///          behaviour so all translation units observe identical semantics when
///          storing or comparing handles.

#include "support/symbol.hpp"

namespace il::support
{
/// @brief Compare two symbols for equality.
///
/// @details Symbols compare by their underlying integer identifier, which
///          uniquely maps to an interned string.  This operation enables value
///          semantics for handles and keeps equality checks free of string
///          comparisons.
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
/// @details Delegates to @ref operator== to maintain consistency between the
///          relational helpers.  The function is `noexcept`, matching the
///          constraints of equality.
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
/// @details Interned strings are assigned identifiers starting at one.  The
///          reserved identifier zero is used as a sentinel for "not found" and
///          propagates as an invalid handle.  The implicit conversion allows
///          idiomatic checks like `if (Symbol s = intern(...))`.
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
/// @details The hash simply returns the underlying numeric identifier because
///          symbols already provide a dense, stable mapping from strings to
///          integers.  This keeps unordered containers of symbols maximally
///          efficient.
///
/// @param s Symbol handle to hash.
/// @return Stable hash derived from the underlying identifier.
size_t hash<il::support::Symbol>::operator()(il::support::Symbol s) const noexcept
{
    return s.id;
}
} // namespace std
