// File: src/il/utils/Utils.hpp
// Purpose: Declares helper functions for IL structures.
// Key invariants: None.
// Ownership/Lifetime: Non-owning pointers to blocks and instructions.
// Links: docs/dev/analysis.md
#pragma once

#include "il/core/BasicBlock.hpp"

namespace il::utils
{

bool inBlock(const il::core::BasicBlock &bb, const il::core::Instr *inst);

bool isTerminator(const il::core::Instr &inst);

} // namespace il::utils
