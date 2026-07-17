//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/Module.cpp
// Purpose: Implement il::core::Module helper routines.
// Key invariants: The module owns the only string interner that may produce
//                 valid Symbol sidecars for declarations, blocks, and
//                 instruction references in the module.
// Ownership/Lifetime: Module owns its StringInterner and all IR declarations;
//                     helpers operate on caller-owned Module instances without
//                     introducing global state.
// Links: docs/internals/codemap.md, docs/internals/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Out-of-line helpers for the top-level IL module container.
/// @details This file centralizes identifier interning so parsers, builders,
///          linkers, and transforms can keep public string fields for
///          round-tripping while also populating compact Symbol sidecars for
///          compiler analyses.

#include "il/core/Module.hpp"

namespace il::core {
namespace {

/// @brief Intern every symbolic reference stored directly on an instruction.
/// @param module Module whose interner owns the resulting symbols.
/// @param instr Instruction to update in place.
/// @post `instr.calleeSymbol` mirrors `instr.callee` when non-empty.
/// @post `instr.labelSymbols` mirrors `instr.labels` in size and order.
void internInstructionIdentifiers(Module &module, Instr &instr) {
    instr.calleeSymbol = instr.callee.empty() ? il::support::Symbol{}
                                             : module.internIdentifier(instr.callee);

    instr.labelSymbols.clear();
    instr.labelSymbols.reserve(instr.labels.size());
    for (const auto &label : instr.labels) {
        instr.labelSymbols.push_back(label.empty() ? il::support::Symbol{}
                                                   : module.internIdentifier(label));
    }
}

} // namespace

/// @brief Intern one non-empty IR identifier in this module.
/// @param text Identifier text to intern.
/// @return Module-owned symbol, or an invalid symbol for empty input.
il::support::Symbol Module::internIdentifier(std::string_view text) {
    if (text.empty())
        return {};
    return symbols.intern(text);
}

/// @brief Resolve an interned module identifier back to text.
/// @param symbol Symbol produced by this module's interner.
/// @return Borrowed view of the interned text, or empty when invalid.
std::string_view Module::lookupIdentifier(il::support::Symbol symbol) const {
    return symbols.lookup(symbol);
}

/// @brief Check whether a symbol belongs to this module interner.
/// @param symbol Symbol handle to validate.
/// @return True when @p symbol names text owned by this module.
bool Module::containsIdentifier(il::support::Symbol symbol) const noexcept {
    return symbols.contains(symbol);
}

/// @brief Rebuild identifier sidecars from public string fields.
/// @post Declaration name symbols, block label symbols, direct callees, and
///       branch label symbols mirror the current strings in the module.
void Module::internOwnedIdentifiers() {
    for (auto &ext : externs)
        ext.nameSymbol = internIdentifier(ext.name);

    for (auto &global : globals)
        global.nameSymbol = internIdentifier(global.name);

    for (auto &function : functions) {
        function.nameSymbol = internIdentifier(function.name);
        for (auto &block : function.blocks) {
            block.labelSymbol = internIdentifier(block.label);
            for (auto &instr : block.instructions)
                internInstructionIdentifiers(*this, instr);
        }
    }
}

} // namespace il::core
