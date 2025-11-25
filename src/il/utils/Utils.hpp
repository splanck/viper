//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/utils/Utils.hpp
// Purpose: Provide small helper utilities for IL blocks and instructions.
// Key invariants: Non-owning queries that depend only on il_core types.
// Ownership/Lifetime: Callers retain ownership of IL structures.
// Links: docs/dev/analysis.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

namespace il::core
{
struct Instr;
struct BasicBlock;
struct Function;
struct Value;
} // namespace il::core

namespace viper::il
{

using Instruction = ::il::core::Instr;
using Block = ::il::core::BasicBlock;

/// @brief Check whether instruction @p I belongs to block @p B.
/// @param I Instruction to look for.
/// @param B Block to scan.
/// @return True if @p I is contained within @p B.
bool belongsToBlock(const Instruction &I, const Block &B);

/// @brief Return the terminator instruction of block @p B.
/// @param B Block to inspect.
/// @return Pointer to the terminator or nullptr if none exists.
Instruction *terminator(Block &B);

/// @brief Determine whether instruction @p I is a terminator.
/// @param I Instruction to inspect.
/// @return True if @p I is br, cbr, ret, or trap.
bool isTerminator(const Instruction &I);

/// @brief Replace all uses of a temporary identifier with a new value.
/// @details Scans all instructions and branch arguments in the function,
///          replacing occurrences of temporary @p tempId with @p replacement.
///          Used by optimization passes to substitute SSA values.
/// @param F Function to modify.
/// @param tempId Temporary identifier to replace.
/// @param replacement New value to substitute.
/// @sideeffect Mutates operands and branch arguments in place.
void replaceAllUses(::il::core::Function &F, unsigned tempId, const ::il::core::Value &replacement);

/// @brief Compute the next available temporary identifier in a function.
/// @details Scans all parameters, block parameters, instruction results,
///          operands, and branch arguments to find the highest temp ID in use.
///          Returns one greater than the maximum, ensuring no collision.
/// @param F Function to analyze.
/// @return First unused temporary identifier.
unsigned nextTempId(const ::il::core::Function &F);

/// @brief Find a basic block by label in @p F.
/// @param F Function to search.
/// @param label Label to match exactly.
/// @return Pointer to the block when found; nullptr otherwise.
Block *findBlock(::il::core::Function &F, std::string_view label);

/// @brief Find a basic block by label in @p F (const overload).
/// @param F Function to search.
/// @param label Label to match exactly.
/// @return Pointer to the block when found; nullptr otherwise.
const Block *findBlock(const ::il::core::Function &F, std::string_view label);

} // namespace viper::il
