//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/IntBasicArithTests.cpp
// Purpose: Validate VM handlers for basic integer arithmetic opcodes
//          (Add, Sub, Mul) including wrapping behavior.
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
void buildBinaryFunction(Module &module, Opcode op, int64_t lhs, int64_t rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(Type::Kind::I64);
    instr.operands.push_back(Value::constInt(lhs));
    instr.operands.push_back(Value::constInt(rhs));
    instr.loc = {1, 1, 1};
    bb.instructions.push_back(instr);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*instr.result));
    bb.instructions.push_back(ret);
}

int64_t runBinary(Opcode op, int64_t lhs, int64_t rhs)
{
    Module module;
    buildBinaryFunction(module, op, lhs, rhs);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    const int64_t minVal = std::numeric_limits<int64_t>::min();
    const int64_t maxVal = std::numeric_limits<int64_t>::max();

    //=========================================================================
    // Add tests (wrapping addition)
    //=========================================================================

    // Basic addition
    assert(runBinary(Opcode::Add, 0, 0) == 0);
    assert(runBinary(Opcode::Add, 1, 2) == 3);
    assert(runBinary(Opcode::Add, -1, 1) == 0);
    assert(runBinary(Opcode::Add, -1, -1) == -2);
    assert(runBinary(Opcode::Add, 100, 200) == 300);

    // Wrapping behavior (unlike IAddOvf, Add wraps silently)
    // MAX + 1 wraps to MIN
    assert(runBinary(Opcode::Add, maxVal, 1) == minVal);
    // MIN + (-1) wraps to MAX
    assert(runBinary(Opcode::Add, minVal, -1) == maxVal);

    // Commutative
    assert(runBinary(Opcode::Add, 5, 7) == runBinary(Opcode::Add, 7, 5));

    //=========================================================================
    // Sub tests (wrapping subtraction)
    //=========================================================================

    // Basic subtraction
    assert(runBinary(Opcode::Sub, 5, 3) == 2);
    assert(runBinary(Opcode::Sub, 3, 5) == -2);
    assert(runBinary(Opcode::Sub, 0, 0) == 0);
    assert(runBinary(Opcode::Sub, -1, -1) == 0);
    assert(runBinary(Opcode::Sub, 10, -5) == 15);

    // Wrapping behavior
    // MIN - 1 wraps to MAX
    assert(runBinary(Opcode::Sub, minVal, 1) == maxVal);
    // MAX - (-1) wraps to MIN
    assert(runBinary(Opcode::Sub, maxVal, -1) == minVal);

    // Identity
    assert(runBinary(Opcode::Sub, 42, 0) == 42);

    //=========================================================================
    // Mul tests (wrapping multiplication)
    //=========================================================================

    // Basic multiplication
    assert(runBinary(Opcode::Mul, 0, 5) == 0);
    assert(runBinary(Opcode::Mul, 1, 5) == 5);
    assert(runBinary(Opcode::Mul, 2, 3) == 6);
    assert(runBinary(Opcode::Mul, -2, 3) == -6);
    assert(runBinary(Opcode::Mul, -2, -3) == 6);
    assert(runBinary(Opcode::Mul, 7, 11) == 77);

    // Commutative
    assert(runBinary(Opcode::Mul, 5, 7) == runBinary(Opcode::Mul, 7, 5));

    // Identity and zero
    assert(runBinary(Opcode::Mul, 42, 1) == 42);
    assert(runBinary(Opcode::Mul, 42, 0) == 0);

    // Powers of 2
    assert(runBinary(Opcode::Mul, 1, 1024) == 1024);
    assert(runBinary(Opcode::Mul, 2, 1024) == 2048);

    // Wrapping behavior (large multiplications wrap)
    // These wrap due to overflow, just verify no crash
    int64_t wrapped = runBinary(Opcode::Mul, maxVal, 2);
    (void)wrapped; // Result wraps, just ensure no crash

    return 0;
}
