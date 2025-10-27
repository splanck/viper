// File: tests/vm/TrapDivideByZeroTests.cpp
// Purpose: Ensure DivideByZero traps report kind and instruction index.
// Key invariants: Diagnostic mentions DivideByZero and instruction #0 for the failing op.
// Ownership/Lifetime: Forks child VM process to capture trap output.
// Links: docs/codemap.md

#include "il/build/IRBuilder.hpp"
#include "tests/common/VmFixture.hpp"

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

    Instr div;
    div.result = builder.reserveTempId();
    div.op = Opcode::SDivChk0;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::constInt(1));
    div.operands.push_back(Value::constInt(0));
    div.loc = {1, 1, 1};
    bb.instructions.push_back(div);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    const bool ok = out.find("Trap @main#0 line 1: DivideByZero (code=0)") != std::string::npos;
    assert(ok && "expected DivideByZero trap diagnostic with instruction index");
    return 0;
}
