// File: src/support/string_interner.h
// Purpose: Declares string interning and symbol types.
// Key invariants: Symbol id 0 is invalid.
// Ownership/Lifetime: Interner owns stored strings.
// Links: docs/class-catalog.md
#pragma once
#include "symbol.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
/// @brief Interns strings to provide stable Symbol identifiers.
/// @invariant Symbol 0 is reserved for invalid.
/// @ownership Stores copies of strings internally.
namespace il::support {
class StringInterner {
public:
  Symbol intern(std::string_view str);
  std::string_view lookup(Symbol sym) const;

private:
  std::unordered_map<std::string, Symbol> map_;
  std::vector<std::string> storage_;
};
} // namespace il::support
