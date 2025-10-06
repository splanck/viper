// File: src/support/string_interner.cpp
// Purpose: Implements string interning facility.
// Key invariants: Symbol id 0 is reserved.
// Ownership/Lifetime: Interner owns interned strings.
// Links: docs/codemap.md

#include "string_interner.hpp"

namespace il::support
{

// Intern the given string by assigning a new symbol if necessary.
//
// The interner stores owned strings in `storage_` and maps them to their
// corresponding `Symbol` via `map_`.  When a string is interned for the first
// time, a copy is appended to `storage_` and a new symbol is created using the
// 1-based index of that storage slot.  If the string already exists, the
// previous symbol is reused.  Symbol id 0 is reserved and never produced.
Symbol StringInterner::intern(std::string_view str)
{
    if (auto it = map_.find(str); it != map_.end())
        return it->second;

    storage_.emplace_back(str);
    Symbol sym{static_cast<uint32_t>(storage_.size())};
    map_.emplace(storage_.back(), sym);
    return sym;
}

// Retrieve the original string for @p sym.
//
// A valid symbol has an id in the range [1, storage_.size()].  Requests outside
// this range, including the reserved id 0, yield an empty view indicating an
// invalid lookup.
std::string_view StringInterner::lookup(Symbol sym) const
{
    if (sym.id == 0 || sym.id > storage_.size())
        return {};
    return storage_[sym.id - 1];
}
} // namespace il::support
