//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/TrapOverflowTests.cpp
// Purpose: Ensure Overflow traps report kind and instruction index. 
// Key invariants: Diagnostic must mention Overflow and instruction #0.
// Ownership/Lifetime: Forks child VM process to capture trap diagnostics.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <limits>
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

    Instr add;
    add.result = builder.reserveTempId();
    add.op = Opcode::IAddOvf;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::constInt(std::numeric_limits<long long>::max()));
    add.operands.push_back(Value::constInt(1));
    add.loc = {1, 1, 1};
    bb.instructions.push_back(add);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    const bool ok = out.find("Trap @main#0 line 1: Overflow (code=0)") != std::string::npos;
    assert(ok && "expected Overflow trap diagnostic with instruction index");
    return 0;
}
