// File: src/support/string_interner.cpp
// Purpose: Implements string interning facility.
// Key invariants: Symbol id 0 is reserved.
// Ownership/Lifetime: Interner owns interned strings.
// Links: docs/class-catalog.md

#include "string_interner.hpp"

namespace il::support
{

Symbol StringInterner::intern(std::string_view str)
{
    auto it = map_.find(std::string(str));
    if (it != map_.end())
        return it->second;
    storage_.emplace_back(str);
    Symbol sym{static_cast<uint32_t>(storage_.size())};
    map_.emplace(storage_.back(), sym);
    return sym;
}

std::string_view StringInterner::lookup(Symbol sym) const
{
    if (sym.id == 0 || sym.id > storage_.size())
        return {};
    return storage_[sym.id - 1];
}
} // namespace il::support
