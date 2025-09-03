// File: tests/il/UtilsTests.cpp
// Purpose: Validate basic IL utilities for blocks and instructions.
// Key invariants: Terminator detection and block membership behave predictably.
// Ownership/Lifetime: Constructs local blocks and instructions.
// Links: docs/dev/il-utils.md

#include "IL/Utils.h"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include <cassert>

using namespace viper::il;
using namespace il::core;

static Instr makeInstr(Opcode op)
{
    Instr I{};
    I.op = op;
    I.type = Type(Type::Kind::Void);
    return I;
}

int main()
{
    // belongsToBlock positive and negative
    Block brBlock;
    brBlock.label = "b";
    brBlock.instructions.push_back(makeInstr(Opcode::Add));
    brBlock.instructions.push_back(makeInstr(Opcode::Br));
    brBlock.terminated = true;
    assert(belongsToBlock(brBlock.instructions[0], brBlock));

    Block other;
    other.label = "o";
    other.instructions.push_back(makeInstr(Opcode::Add));
    assert(!belongsToBlock(brBlock.instructions[0], other));

    // terminator and isTerminator for Br
    assert(terminator(brBlock) == &brBlock.instructions[1]);
    assert(isTerminator(brBlock.instructions[1]));
    assert(!isTerminator(brBlock.instructions[0]));

    // CBr
    Block cbrBlock;
    cbrBlock.instructions.push_back(makeInstr(Opcode::CBr));
    cbrBlock.terminated = true;
    assert(terminator(cbrBlock) == &cbrBlock.instructions[0]);

    // Ret
    Block retBlock;
    retBlock.instructions.push_back(makeInstr(Opcode::Ret));
    retBlock.terminated = true;
    assert(terminator(retBlock) == &retBlock.instructions[0]);

    // Trap
    Block trapBlock;
    trapBlock.instructions.push_back(makeInstr(Opcode::Trap));
    trapBlock.terminated = true;
    assert(terminator(trapBlock) == &trapBlock.instructions[0]);

    // No terminator
    Block none;
    none.instructions.push_back(makeInstr(Opcode::Add));
    assert(terminator(none) == nullptr);

    return 0;
}
