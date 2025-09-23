// File: src/support/string_interner.hpp
// Purpose: Declares string interning and symbol types.
// Key invariants: Symbol id 0 is invalid.
// Ownership/Lifetime: Interner owns stored strings.
// Links: docs/codemap.md
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
    /// Interns a string to produce a stable symbol for repeated use.
    ///
    /// Stores a copy of @p str if it has not been seen before and assigns it a
    /// new Symbol. Subsequent calls with the same string yield the existing
    /// Symbol without duplicating storage, enabling fast comparisons and
    /// lookups.
    /// @param str String to intern.
    /// @return Symbol uniquely identifying the interned string.
    Symbol intern(std::string_view str);

    /// Retrieves the original string associated with a Symbol.
    ///
    /// Use to obtain the text for a symbol returned by intern(), for example in
    /// diagnostics or reverse mappings. Passing an invalid Symbol yields an
    /// empty view.
    /// @param sym Symbol previously returned by intern().
    /// @return View of the interned string.
    std::string_view lookup(Symbol sym) const;

  private:
    /// Maps string content to assigned symbols for O(1) lookup during interning.
    std::unordered_map<std::string, Symbol> map_;
    /// Retains copies of interned strings so lookups return stable views.
    std::vector<std::string> storage_;
};
} // namespace il::support
