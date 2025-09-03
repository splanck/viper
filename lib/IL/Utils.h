// File: lib/IL/Utils.h
// Purpose: Declare basic IL block/instruction helpers for passes.
// Key invariants: Operates solely on IL core structures with no analysis deps.
// Ownership/Lifetime: Functions view caller-owned blocks and instructions.
// Links: docs/dev/il-utils.md
#pragma once

namespace il::core
{
struct Instr;
struct BasicBlock;
using Block = BasicBlock;
} // namespace il::core

namespace viper::il
{

using Instruction = ::il::core::Instr;
using Block = ::il::core::Block;

/// @brief Check whether instruction @p I is contained in block @p B.
/// @param I Instruction to test.
/// @param B Block to inspect.
/// @return True if @p I resides within @p B.
bool belongsToBlock(const Instruction &I, const Block &B);

/// @brief Retrieve terminator of block @p B if present.
/// @param B Block whose terminator is queried.
/// @return Pointer to terminator instruction or nullptr if absent.
Instruction *terminator(Block &B);

/// @brief Determine if instruction @p I is a control-flow terminator.
/// @param I Instruction to inspect.
/// @return True for br/cbr/ret/trap; false otherwise.
bool isTerminator(const Instruction &I);

} // namespace viper::il
