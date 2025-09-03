// File: src/il/utils/Utils.cpp
// Purpose: Implement miscellaneous IL helper routines.
// Key invariants: Inputs reference valid instructions and blocks.
// Ownership/Lifetime: No ownership transfer.
// Links: docs/class-catalog.md

#include "il/utils/Utils.hpp"

namespace il::util
{

bool inBlock(const core::Instr &inst, const core::BasicBlock &block)
{
    for (const auto &i : block.instructions)
        if (&i == &inst)
            return true;
    return false;
}

} // namespace il::util
