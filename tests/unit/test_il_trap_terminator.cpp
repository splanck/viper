// File: tests/unit/test_il_trap_terminator.cpp
// Purpose: Ensure verifier rejects instructions following a trap.
// Key invariants: A trap terminates a block and must be final.
// Ownership/Lifetime: Constructs module and verifier on the stack.
// Links: docs/il-spec.md

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/verify/Verifier.hpp"
#include <cassert>
#include <sstream>

int main()
{
    using namespace il::core;
    Module m;
    Function f;
    f.name = "f";
    f.retType = Type(Type::Kind::Void);

    BasicBlock bb;
    bb.label = "entry";

    Instr trap;
    trap.op = Opcode::Trap;
    trap.type = Type(Type::Kind::Void);
    bb.instructions.push_back(trap);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    bb.instructions.push_back(ret);

    f.blocks.push_back(bb);
    m.functions.push_back(f);

    std::ostringstream err;
    bool ok = il::verify::Verifier::verify(m, err);
    assert(!ok);
    assert(err.str().find("terminator") != std::string::npos);
    return 0;
}
