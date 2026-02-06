//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/TruncZextTests.cpp
// Purpose: Validate VM handlers for Trunc1 and Zext1 opcodes
//          (1-bit truncation and zero-extension).
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace il::core;

namespace
{
// Build function for Trunc1: truncate i64 to i1 (boolean)
void buildTrunc1Function(Module &module, int64_t val)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr trunc;
    trunc.result = builder.reserveTempId();
    trunc.op = Opcode::Trunc1;
    trunc.type = Type(Type::Kind::I1);
    trunc.operands.push_back(Value::constInt(val));
    trunc.loc = {1, 1, 1};
    bb.instructions.push_back(trunc);

    // Zero-extend back to i64 for return
    Instr zext;
    zext.result = builder.reserveTempId();
    zext.op = Opcode::Zext1;
    zext.type = Type(Type::Kind::I64);
    zext.operands.push_back(Value::temp(*trunc.result));
    zext.loc = {1, 1, 1};
    bb.instructions.push_back(zext);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*zext.result));
    bb.instructions.push_back(ret);
}

// Build function for Zext1 directly from boolean constant
void buildZext1DirectFunction(Module &module, int64_t boolVal)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // Create a comparison that yields the boolean value
    Instr cmp;
    cmp.result = builder.reserveTempId();
    cmp.op = Opcode::ICmpNe;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(Value::constInt(boolVal));
    cmp.operands.push_back(Value::constInt(0));
    cmp.loc = {1, 1, 1};
    bb.instructions.push_back(cmp);

    // Zero-extend to i64
    Instr zext;
    zext.result = builder.reserveTempId();
    zext.op = Opcode::Zext1;
    zext.type = Type(Type::Kind::I64);
    zext.operands.push_back(Value::temp(*cmp.result));
    zext.loc = {1, 1, 1};
    bb.instructions.push_back(zext);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*zext.result));
    bb.instructions.push_back(ret);
}

int64_t runTrunc1(int64_t val)
{
    Module module;
    buildTrunc1Function(module, val);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

int64_t runZext1Direct(int64_t boolVal)
{
    Module module;
    buildZext1DirectFunction(module, boolVal);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    //=========================================================================
    // Trunc1 tests (truncate to i1, keeping only LSB)
    //=========================================================================

    // 0 -> 0
    assert(runTrunc1(0) == 0);

    // Odd numbers (LSB = 1) -> 1
    assert(runTrunc1(1) == 1);
    assert(runTrunc1(3) == 1);
    assert(runTrunc1(5) == 1);
    assert(runTrunc1(7) == 1);
    assert(runTrunc1(-1) == 1);
    assert(runTrunc1(-3) == 1);

    // Even numbers (LSB = 0) -> 0
    assert(runTrunc1(2) == 0);
    assert(runTrunc1(4) == 0);
    assert(runTrunc1(100) == 0);
    assert(runTrunc1(-2) == 0);
    assert(runTrunc1(-4) == 0);

    // Edge cases
    assert(runTrunc1(std::numeric_limits<int64_t>::max()) == 1); // MAX is odd
    assert(runTrunc1(std::numeric_limits<int64_t>::min()) == 0); // MIN is even

    //=========================================================================
    // Zext1 tests (zero-extend i1 to i64)
    //=========================================================================

    // 0 -> 0
    assert(runZext1Direct(0) == 0);

    // 1 -> 1
    assert(runZext1Direct(1) == 1);

    // Any non-zero value comparison yields 1
    assert(runZext1Direct(42) == 1);
    assert(runZext1Direct(-1) == 1);
    assert(runZext1Direct(100) == 1);

    return 0;
}
