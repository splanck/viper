// File: lib/IL/Utils.cpp
// Purpose: Implement basic IL block/instruction helpers for passes.
// Key invariants: Uses only IL core constructs without analysis dependencies.
// Ownership/Lifetime: Views data owned by caller; no allocations.
// Links: docs/dev/il-utils.md

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
            return true;
    }
    return false;
}

Instruction *terminator(Block &B)
{
    if (B.instructions.empty())
        return nullptr;
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
