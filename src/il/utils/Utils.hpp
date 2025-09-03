// File: src/il/utils/Utils.hpp
// Purpose: Miscellaneous helpers for inspecting IL structures.
// Key invariants: None.
// Ownership/Lifetime: Non-owning references to blocks and instructions.
// Links: docs/dev/analysis.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

namespace il::utils
{

bool isInstrInBlock(const il::core::Instr &instr, const il::core::BasicBlock &block);

} // namespace il::utils
