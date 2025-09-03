// File: src/il/Utils.cpp
// Purpose: Implements IL helper functions for instruction queries.
// Key invariants: None.
// Ownership/Lifetime: Non-owning views into existing structures.
// Links: docs/dev/analysis.md

#include "il/Utils.hpp"

namespace il::util
{

bool isInBlock(const il::core::BasicBlock &bb, const il::core::Instr &inst)
{
    for (const auto &i : bb.instructions)
    {
        if (&i == &inst)
            return true;
    }
    return false;
}

const il::core::BasicBlock *findBlock(const il::core::Function &fn, const il::core::Instr &inst)
{
    for (const auto &bb : fn.blocks)
    {
        if (isInBlock(bb, inst))
            return &bb;
    }
    return nullptr;
}

} // namespace il::util
