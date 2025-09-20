// File: tests/il/UtilsTests.cpp
// Purpose: Verify IL utility helpers for block membership and terminators.
// Key invariants: Helpers correctly identify instruction containment and terminators.
// Ownership/Lifetime: Constructs local IL blocks and instructions.
// Links: docs/dev/analysis.md

#include "il/utils/Utils.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include <cassert>

int main()
{
    using namespace viper::il;
    using il::core::BasicBlock;
    using il::core::Instr;
    using il::core::Opcode;

    // Block with a single non-terminator.
    BasicBlock b;
    b.label = "b";
    b.instructions.emplace_back();
    Instr &add = b.instructions.back();
    add.op = Opcode::Add;
    assert(belongsToBlock(add, b));
    Instr other;
    other.op = Opcode::Add;
    assert(!belongsToBlock(other, b));
    assert(!isTerminator(add));
    assert(terminator(b) == nullptr);

    auto checkTerm = [](Opcode op)
    {
        BasicBlock blk;
        blk.label = "t";
        blk.instructions.emplace_back();
        blk.instructions.back().op = Opcode::Add;
        blk.instructions.emplace_back();
        Instr &term = blk.instructions.back();
        term.op = op;
        blk.terminated = true;
        assert(isTerminator(term));
        assert(terminator(blk) == &term);
    };

    checkTerm(Opcode::Br);
    checkTerm(Opcode::CBr);
    checkTerm(Opcode::Ret);
    checkTerm(Opcode::Trap);

    return 0;
}
