// File: src/il/utils/Utils.cpp
// Purpose: Implements small IL helper routines.
// Key invariants: None.
// Ownership/Lifetime: Does not assume ownership of IL objects.
// Links: docs/dev/analysis.md

#include "il/utils/Utils.hpp"
#include <algorithm>

namespace il::utils
{

bool isInstrInBlock(const il::core::Instr &instr, const il::core::BasicBlock &block)
{
    return std::any_of(block.instructions.begin(),
                       block.instructions.end(),
                       [&](const il::core::Instr &i) { return &i == &instr; });
}

} // namespace il::utils
