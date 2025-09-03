// File: src/il/utils/Utils.hpp
// Purpose: Miscellaneous IL helper routines.
// Key invariants: Functions operate on existing instructions and blocks.
// Ownership/Lifetime: Does not take ownership of inputs.
// Links: docs/class-catalog.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

namespace il::util
{

/// @brief Check if instruction @p inst exists in @p block.
/// @param inst Instruction to test.
/// @param block Basic block to search.
/// @return True if @p inst resides in @p block.
bool inBlock(const core::Instr &inst, const core::BasicBlock &block);

} // namespace il::util
