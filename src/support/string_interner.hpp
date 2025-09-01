// File: src/support/string_interner.hpp
// Purpose: Declares string interning and symbol types.
// Key invariants: Symbol id 0 is invalid.
// Ownership/Lifetime: Interner owns stored strings.
// Links: docs/class-catalog.md
#pragma once

#include "symbol.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::support
{

/// @brief Interns strings to provide stable Symbol identifiers.
/// @invariant Symbol 0 is reserved for invalid.
/// @ownership Stores copies of strings internally.
class StringInterner
{
  public:
    /// @brief Intern @p str and return its Symbol.
    /// @param str String to intern.
    /// @return Stable symbol identifying the string.
    Symbol intern(std::string_view str);

    /// @brief Look up string for symbol @p sym.
    /// @param sym Symbol previously returned by intern().
    /// @return View of the interned string.
    std::string_view lookup(Symbol sym) const;

  private:
    std::unordered_map<std::string, Symbol> map_;
    std::vector<std::string> storage_;
};
} // namespace il::support
