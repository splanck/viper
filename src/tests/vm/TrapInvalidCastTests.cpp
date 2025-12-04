//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/TrapInvalidCastTests.cpp
// Purpose: Ensure InvalidCast traps report kind and instruction index.
// Key invariants: Diagnostic mentions InvalidCast and instruction #0 for cast op.
// Ownership/Lifetime: Uses forked VM process to capture stderr.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cmath>
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

    Instr cast;
    cast.result = builder.reserveTempId();
    cast.op = Opcode::CastFpToSiRteChk;
    cast.type = Type(Type::Kind::I64);
    cast.operands.push_back(Value::constFloat(std::numeric_limits<double>::quiet_NaN()));
    cast.loc = {1, 1, 1};
    bb.instructions.push_back(cast);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    // Format: "Trap @function:block#ip line N: Kind (code=C)"
    const bool ok = out.find("Trap @main:entry#0 line 1: InvalidCast (code=0)") != std::string::npos;
    assert(ok && "expected InvalidCast trap diagnostic with instruction index");
    return 0;
}
