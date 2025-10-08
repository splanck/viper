/**
 * @file symbol.cpp
 * @brief Provides comparison helpers and utilities for the `Symbol` type.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     Symbols are lightweight wrappers around 32-bit identifiers produced by
 *     the string interner.  Identifier value `0` is reserved to indicate the
 *     absence of a symbol, while all other values correspond to interned
 *     strings.
 */

#include "support/symbol.hpp"

namespace il::support
{
/**
 * @brief Tests whether two symbol handles refer to the same interned string.
 *
 * Because `Symbol` stores only a numeric identifier, equality reduces to
 * comparing the identifiers.  This comparison is noexcept and can be used in
 * containers or algorithms without throwing.
 *
 * @param a First symbol to compare.
 * @param b Second symbol to compare.
 * @return `true` when the identifiers are equal.
 */
bool operator==(Symbol a, Symbol b) noexcept
{
    return a.id == b.id;
}

/**
 * @brief Tests whether two symbol handles refer to different interned strings.
 *
 * Inequality is implemented directly in terms of the stored identifiers and is
 * likewise noexcept.
 *
 * @param a First symbol to compare.
 * @param b Second symbol to compare.
 * @return `true` when the identifiers differ.
 */
bool operator!=(Symbol a, Symbol b) noexcept
{
    return a.id != b.id;
}

/**
 * @brief Converts the symbol to a boolean indicating whether it is valid.
 *
 * The implicit conversion returns `true` for any identifier other than zero,
 * allowing callers to use symbols in conditionals to test for presence.
 *
 * @return `true` when the symbol denotes an interned string.
 */
Symbol::operator bool() const noexcept
{
    return id != 0;
}
} // namespace il::support

namespace std
{
/**
 * @brief Computes a hash code for a `Symbol` so it can be stored in unordered containers.
 *
 * The hash uses the numeric identifier directly, preserving the distribution
 * characteristics of the underlying interner index while remaining
 * deterministic across process runs.
 *
 * @param s Symbol to hash.
 * @return Hash code corresponding to the symbol identifier.
 */
size_t hash<il::support::Symbol>::operator()(il::support::Symbol s) const noexcept
{
    return s.id;
}
} // namespace std

