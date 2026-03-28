//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/SymbolTable.cpp
// Purpose: Implementation of symbol table for object file generation.
// Key invariants:
//   - Index 0 is always the null entry
//   - findOrAdd creates External symbols for unknown names
// Ownership/Lifetime:
//   - See SymbolTable.hpp
// Links: codegen/common/objfile/SymbolTable.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/SymbolTable.hpp"

namespace viper::codegen::objfile {

SymbolTable::SymbolTable() {
    // Index 0 is the null symbol (ELF requirement, harmless for other formats).
    symbols_.push_back(Symbol{"", SymbolBinding::Local, SymbolSection::Undefined, 0, 0});
}

uint32_t SymbolTable::add(Symbol sym) {
    auto index = static_cast<uint32_t>(symbols_.size());
    if (!sym.name.empty())
        nameIndex_[sym.name] = index;
    symbols_.push_back(std::move(sym));
    return index;
}

uint32_t SymbolTable::findOrAdd(const std::string &name) {
    auto it = nameIndex_.find(name);
    if (it != nameIndex_.end())
        return it->second;

    // Not found — declare as external.
    return add(Symbol{name, SymbolBinding::External, SymbolSection::Undefined, 0, 0});
}

} // namespace viper::codegen::objfile
