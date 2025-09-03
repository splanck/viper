// File: lib/IL/Utils.h
// Purpose: Provide small helper utilities for IL blocks and instructions.
// Key invariants: Non-owning queries that depend only on il_core types.
// Ownership/Lifetime: Callers retain ownership of IL structures.
// Links: docs/dev/analysis.md
#pragma once

namespace il::core
{
struct Instr;
struct BasicBlock;
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

} // namespace viper::il
