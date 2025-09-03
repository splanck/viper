// File: lib/IL/Utils.cpp
// Purpose: Implement small IL helper utilities for blocks and instructions.
// Key invariants: Operates on il_core structures without additional dependencies.
// Ownership/Lifetime: Non-owning views; caller manages lifetimes.
// Links: docs/dev/analysis.md
#include "IL/Utils.h"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

namespace viper::il
{

bool belongsToBlock(const Instruction &I, const Block &B)
{
    for (const auto &inst : B.instructions)
    {
        if (&inst == &I)
        {
            return true;
        }
    }
    return false;
}

Instruction *terminator(Block &B)
{
    if (B.instructions.empty())
    {
        return nullptr;
    }
    Instruction &last = B.instructions.back();
    return isTerminator(last) ? &last : nullptr;
}

bool isTerminator(const Instruction &I)
{
    using ::il::core::Opcode;
    switch (I.op)
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

} // namespace viper::il
