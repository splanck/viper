//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LoweringContext.cpp
// Purpose: Provide the stateful helpers used when lowering BASIC surface syntax
//          into IL instructions and blocks.
// Key invariants: Slot names, block labels, and string identifiers are stable
//                 and deterministic within a compilation unit.
// Links: docs/codemap/basic.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements caching helpers for the BASIC lowering pipeline.
/// @details The lowering context bundles references to the IR builder and the
///          function being populated.  Housing the helper logic out-of-line keeps
///          the header small and ensures all mapping rules remain documented in
///          a single location.

#include "frontends/basic/LoweringContext.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"

namespace il::frontends::basic
{

/// @brief Construct a lowering context for a BASIC function.
/// @details The context stores references to the builder and destination
///          function so subsequent helpers can materialize blocks, stack slots,
///          and literals without re-threading these dependencies through each
///          call site.
/// @param builder IR builder used to create blocks and instructions.
/// @param func Function that will receive lowered IR.
LoweringContext::LoweringContext(build::IRBuilder &builder, core::Function &func)
    : builder(builder), function(func)
{
}

/// @brief Retrieve a stack slot name for BASIC variable @p name, creating one if needed.
/// @details Lowers variables into `alloca`-style stack slots.  Previously issued
///          names are cached so repeated lookups avoid allocating duplicate
///          slots.  When creating a new slot the method prefixes the BASIC name
///          with "%" and appends `_slot` to keep generated IR descriptive.
/// @param name BASIC variable identifier.
/// @return Unique slot label for the variable.
std::string LoweringContext::getOrCreateSlot(const std::string &name)
{
    auto it = varSlots.find(name);
    if (it != varSlots.end())
        return it->second;
    std::string slot = "%" + name + "_slot";
    varSlots[name] = slot;
    return slot;
}

/// @brief Retrieve or create an IR block for BASIC line number @p line.
/// @details BASIC line numbers serve as natural block labels.  The helper keeps
///          a map keyed by the integer line so structured control-flow lowering
///          can reuse blocks as loops and gotos are processed.  Missing entries
///          result in new blocks inserted via the IR builder with mangled names
///          that remain stable for diagnostics.
/// @param line Line number in the source program.
/// @return Pointer to the corresponding basic block.
core::BasicBlock *LoweringContext::getOrCreateBlock(int line)
{
    auto it = blocks.find(line);
    if (it != blocks.end())
        return it->second;
    std::string label = mangler.block("L" + std::to_string(line));
    core::BasicBlock &bb = builder.addBlock(function, label);
    blocks[line] = &bb;
    return &bb;
}

/// @brief Intern the BASIC string literal @p value and return its IR symbol.
///
/// Maintains a mapping from literal text to generated identifiers, reusing
/// existing entries without consuming new IDs. When a string is first seen it
/// receives a label derived from an incrementing counter to keep identifiers
/// stable across the module.  The names are fed into `addGlobalStr` to produce
/// one global per unique literal, avoiding redundant data in the output module.
/// @param value BASIC string literal to intern.
/// @return Stable label bound to the string literal.
std::string LoweringContext::getOrAddString(const std::string &value)
{
    auto it = strings.find(value);
    if (it != strings.end())
        return it->second;
    std::string name = ".L" + std::to_string(nextStringId++);
    strings[value] = name;
    return name;
}

} // namespace il::frontends::basic
