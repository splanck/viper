// File: tests/vm/TrapDomainErrorTests.cpp
// Purpose: Verify DomainError trap diagnostics include kind and instruction index.
// Key invariants: Trap output must mention DomainError and #0 for the trap terminator.
// Ownership/Lifetime: Spawns child process to capture VM stderr.
// Links: docs/codemap.md

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <string>

using namespace il::core;

int main()
{
    using viper::tests::VmFixture;

    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr trap;
    trap.op = Opcode::Trap;
    trap.type = Type(Type::Kind::Void);
    trap.loc = {1, 1, 1};
    bb.instructions.push_back(trap);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    const bool ok = out.find("Trap @main#0 line 1: DomainError (code=0)") != std::string::npos;
    assert(ok && "expected DomainError trap diagnostic with instruction index");
    return 0;
}
