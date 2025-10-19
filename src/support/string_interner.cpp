//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the string interner that assigns stable Symbol handles to unique
// strings.  The interner owns the canonical copies of the strings and provides
// constant-time lookup from handles back to their original text.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Realises the compact string interning facility used across IL.
/// @details Symbols provide fast, comparable handles for compiler identifiers.
///          This translation unit implements the storage and lookup routines so
///          headers remain lightweight while providing a single source of truth
///          for handle semantics.

#include "string_interner.hpp"

namespace il::support
{

StringInterner::StringInterner(uint32_t maxSymbols) noexcept : maxSymbols_(maxSymbols)
{
}

/// @brief Intern the given string and return its Symbol handle.
///
/// @details The interner stores owned strings in `storage_` and maps them to
///          their corresponding `Symbol` via `map_`.  When a string is interned
///          for the first time, a copy is appended to `storage_` and a new
///          symbol is created using the 1-based index of that storage slot.
///          Reinterning the same view simply returns the existing handle,
///          guaranteeing pointer stability.  Symbol id zero is reserved and
///          never produced so clients can use it as an "invalid" sentinel.
///
/// @param str String view to intern; copied when not already stored.
/// @return Stable Symbol handle identifying the string.
Symbol StringInterner::intern(std::string_view str)
{
    auto it = map_.find(str);
    if (it != map_.end())
        return it->second;
    if (storage_.size() >= maxSymbols_)
        return {};
    storage_.emplace_back(str);
    Symbol sym{static_cast<uint32_t>(storage_.size())};
    map_.emplace(storage_.back(), sym);
    return sym;
}

/// @brief Retrieve the interned string associated with a Symbol handle.
///
/// @details Valid symbols have identifiers in the range [1, storage_.size()].
///          Requests outside this range, including the reserved id zero, yield
///          an empty view to signal an invalid lookup.  The returned view refers
///          directly to owned storage and therefore inherits its lifetime from
///          the interner.
///
/// @param sym Symbol handle to resolve.
/// @return View of the stored string, or empty view for invalid handles.
std::string_view StringInterner::lookup(Symbol sym) const
{
    if (sym.id == 0 || sym.id > storage_.size())
        return {};
    return storage_[sym.id - 1];
}
} // namespace il::support
