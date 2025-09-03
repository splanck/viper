// File: src/il/utils/Utils.cpp
// Purpose: Implements helper functions for IL structures.
// Key invariants: None.
// Ownership/Lifetime: Non-owning pointers to inputs.
// Links: docs/dev/analysis.md

#include "il/utils/Utils.hpp"
#include "il/core/Opcode.hpp"

namespace il::utils
{

bool inBlock(const il::core::BasicBlock &bb, const il::core::Instr *inst)
{
    if (!inst)
        return false;
    for (const auto &i : bb.instructions)
        if (&i == inst)
            return true;
    return false;
}

bool isTerminator(const il::core::Instr &inst)
{
    using il::core::Opcode;
    switch (inst.op)
    {
        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::Ret:
        case Opcode::Trap:
            return true;
        default:
            return false;
    }
}

} // namespace il::utils
