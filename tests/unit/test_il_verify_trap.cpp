// File: tests/unit/test_il_verify_trap.cpp
// Purpose: Ensure Verifier accepts blocks terminated by trap.
// Key invariants: Blocks ending with Opcode::Trap pass verification.
// Ownership/Lifetime: Constructs module locally for verification.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include <cassert>
#include <sstream>

int main()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "f";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock bb;
    bb.label = "entry";

    Instr trap;
    trap.op = Opcode::Trap;
    bb.instructions.push_back(trap);
    bb.terminated = true;

    fn.blocks.push_back(bb);
    m.functions.push_back(fn);

    std::ostringstream diag;
    auto ve = il::verify::Verifier::verify(m);
    if (!ve)
    {
        il::support::printDiag(ve.error(), diag);
    }
    assert(ve);
    assert(diag.str().empty());

    return 0;
}
