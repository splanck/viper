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

#include "string_interner.hpp"

namespace il::support
{

/// @brief Intern the given string and return its Symbol handle.
///
/// The interner stores owned strings in `storage_` and maps them to their
/// corresponding `Symbol` via `map_`.  When a string is interned for the first
/// time, a copy is appended to `storage_` and a new symbol is created using the
/// 1-based index of that storage slot.  If the string already exists, the
/// previous symbol is reused.  Symbol id zero is reserved and never produced.
///
/// @param str String view to intern; copied when not already stored.
/// @return Stable Symbol handle identifying the string.
Symbol StringInterner::intern(std::string_view str)
{
    auto it = map_.find(str);
    if (it != map_.end())
        return it->second;
    storage_.emplace_back(str);
    Symbol sym{static_cast<uint32_t>(storage_.size())};
    map_.emplace(storage_.back(), sym);
    return sym;
}

/// @brief Retrieve the interned string associated with a Symbol handle.
///
/// Valid symbols have identifiers in the range [1, storage_.size()].  Requests
/// outside this range, including the reserved id zero, yield an empty view to
/// signal an invalid lookup.
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
