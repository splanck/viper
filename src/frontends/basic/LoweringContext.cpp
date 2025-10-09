//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the transient state used while lowering BASIC source into IL.  The
// context tracks slot names, basic blocks, and interned string literals so that
// lowering passes can generate deterministic identifiers without global state.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/LoweringContext.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"

namespace il::frontends::basic
{

/// @brief Construct a lowering context for a BASIC function.
/// @param builder IR builder used to create blocks and instructions.
/// @param func Function that will receive lowered IR.
LoweringContext::LoweringContext(build::IRBuilder &builder, core::Function &func)
    : builder(builder), function(func)
{
}

/// @brief Retrieve (or create) the slot label for a BASIC variable.
///
/// Slot names are generated lazily and cached so subsequent lookups reuse the
/// same identifier.  New slot names take the form `%<name>_slot`.
///
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
///
/// The context delegates to the mangler to produce a deterministic block name
/// derived from the line number and uses the builder to append the block to the
/// current function.
///
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
/// existing entries without consuming new IDs.  When a string is first seen it
/// receives a label derived from an incrementing counter to keep identifiers
/// stable across the module.
///
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
