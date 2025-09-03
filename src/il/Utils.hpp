// File: src/il/Utils.hpp
// Purpose: Helper functions for inspecting IL instruction placement.
// Key invariants: None.
// Ownership/Lifetime: Functions operate on existing structures.
// Links: docs/dev/analysis.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

namespace il::util
{

/// @brief Check if instruction @p inst resides in block @p bb.
/// @param bb Basic block to search.
/// @param inst Instruction to locate.
/// @return True if @p inst is contained in @p bb.
bool isInBlock(const il::core::BasicBlock &bb, const il::core::Instr &inst);

/// @brief Find block containing instruction @p inst in function @p fn.
/// @param fn Function to inspect.
/// @param inst Instruction to find.
/// @return Pointer to containing block, or nullptr if not found.
const il::core::BasicBlock *findBlock(const il::core::Function &fn, const il::core::Instr &inst);

} // namespace il::util
