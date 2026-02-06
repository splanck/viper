//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/FloatCompareTests.cpp
// Purpose: Validate VM handlers for floating-point comparison opcodes
//          including NaN handling and special value comparisons.
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace il::core;

namespace
{
// Helper to convert double to int64_t bit pattern for Value::constInt
int64_t doubleBits(double d)
{
    int64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    return bits;
}

void buildFloatCompareFunction(Module &module, Opcode op, double lhs, double rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I1), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // Create ConstF64 for lhs
    Instr lhsInstr;
    lhsInstr.result = builder.reserveTempId();
    lhsInstr.op = Opcode::ConstF64;
    lhsInstr.type = Type(Type::Kind::F64);
    lhsInstr.operands.push_back(Value::constInt(doubleBits(lhs)));
    lhsInstr.loc = {1, 1, 1};
    bb.instructions.push_back(lhsInstr);

    // Create ConstF64 for rhs
    Instr rhsInstr;
    rhsInstr.result = builder.reserveTempId();
    rhsInstr.op = Opcode::ConstF64;
    rhsInstr.type = Type(Type::Kind::F64);
    rhsInstr.operands.push_back(Value::constInt(doubleBits(rhs)));
    rhsInstr.loc = {1, 1, 1};
    bb.instructions.push_back(rhsInstr);

    // Perform the comparison
    Instr cmpInstr;
    cmpInstr.result = builder.reserveTempId();
    cmpInstr.op = op;
    cmpInstr.type = Type(Type::Kind::I1);
    cmpInstr.operands.push_back(Value::temp(*lhsInstr.result));
    cmpInstr.operands.push_back(Value::temp(*rhsInstr.result));
    cmpInstr.loc = {1, 1, 1};
    bb.instructions.push_back(cmpInstr);

    // Return result
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*cmpInstr.result));
    bb.instructions.push_back(ret);
}

bool runFloatCompare(Opcode op, double lhs, double rhs)
{
    Module module;
    buildFloatCompareFunction(module, op, lhs, rhs);
    viper::tests::VmFixture fixture;
    int64_t result = fixture.run(module);
    assert(result == 0 || result == 1);
    return result == 1;
}

} // namespace

int main()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double negInf = -std::numeric_limits<double>::infinity();
    const double denorm = std::numeric_limits<double>::denorm_min();

    //=========================================================================
    // FCmpEQ tests
    //=========================================================================

    // Basic equality
    assert(runFloatCompare(Opcode::FCmpEQ, 1.0, 1.0) == true);
    assert(runFloatCompare(Opcode::FCmpEQ, 1.0, 2.0) == false);
    assert(runFloatCompare(Opcode::FCmpEQ, 0.0, 0.0) == true);
    assert(runFloatCompare(Opcode::FCmpEQ, -0.0, 0.0) == true); // -0 == +0

    // NaN comparisons (NaN != anything, including itself)
    assert(runFloatCompare(Opcode::FCmpEQ, nan, nan) == false);
    assert(runFloatCompare(Opcode::FCmpEQ, nan, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpEQ, 1.0, nan) == false);

    // Infinity
    assert(runFloatCompare(Opcode::FCmpEQ, inf, inf) == true);
    assert(runFloatCompare(Opcode::FCmpEQ, negInf, negInf) == true);
    assert(runFloatCompare(Opcode::FCmpEQ, inf, negInf) == false);

    // Denormals
    assert(runFloatCompare(Opcode::FCmpEQ, denorm, denorm) == true);
    assert(runFloatCompare(Opcode::FCmpEQ, denorm, 0.0) == false);

    //=========================================================================
    // FCmpNE tests
    //=========================================================================

    assert(runFloatCompare(Opcode::FCmpNE, 1.0, 2.0) == true);
    assert(runFloatCompare(Opcode::FCmpNE, 1.0, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpNE, -0.0, 0.0) == false);

    // NaN: NaN != anything is true
    assert(runFloatCompare(Opcode::FCmpNE, nan, nan) == true);
    assert(runFloatCompare(Opcode::FCmpNE, nan, 1.0) == true);
    assert(runFloatCompare(Opcode::FCmpNE, 1.0, nan) == true);

    //=========================================================================
    // FCmpLT tests
    //=========================================================================

    assert(runFloatCompare(Opcode::FCmpLT, 1.0, 2.0) == true);
    assert(runFloatCompare(Opcode::FCmpLT, 2.0, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpLT, 1.0, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpLT, -1.0, 0.0) == true);
    assert(runFloatCompare(Opcode::FCmpLT, negInf, inf) == true);
    assert(runFloatCompare(Opcode::FCmpLT, negInf, 0.0) == true);

    // NaN: all ordered comparisons with NaN are false
    assert(runFloatCompare(Opcode::FCmpLT, nan, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpLT, 1.0, nan) == false);
    assert(runFloatCompare(Opcode::FCmpLT, nan, nan) == false);

    //=========================================================================
    // FCmpLE tests
    //=========================================================================

    assert(runFloatCompare(Opcode::FCmpLE, 1.0, 2.0) == true);
    assert(runFloatCompare(Opcode::FCmpLE, 1.0, 1.0) == true);
    assert(runFloatCompare(Opcode::FCmpLE, 2.0, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpLE, -0.0, 0.0) == true);

    // NaN
    assert(runFloatCompare(Opcode::FCmpLE, nan, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpLE, 1.0, nan) == false);

    //=========================================================================
    // FCmpGT tests
    //=========================================================================

    assert(runFloatCompare(Opcode::FCmpGT, 2.0, 1.0) == true);
    assert(runFloatCompare(Opcode::FCmpGT, 1.0, 2.0) == false);
    assert(runFloatCompare(Opcode::FCmpGT, 1.0, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpGT, inf, negInf) == true);
    assert(runFloatCompare(Opcode::FCmpGT, inf, 0.0) == true);

    // NaN
    assert(runFloatCompare(Opcode::FCmpGT, nan, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpGT, 1.0, nan) == false);

    //=========================================================================
    // FCmpGE tests
    //=========================================================================

    assert(runFloatCompare(Opcode::FCmpGE, 2.0, 1.0) == true);
    assert(runFloatCompare(Opcode::FCmpGE, 1.0, 1.0) == true);
    assert(runFloatCompare(Opcode::FCmpGE, 1.0, 2.0) == false);
    assert(runFloatCompare(Opcode::FCmpGE, 0.0, -0.0) == true);

    // NaN
    assert(runFloatCompare(Opcode::FCmpGE, nan, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpGE, 1.0, nan) == false);

    //=========================================================================
    // FCmpOrd tests (ordered: true if neither operand is NaN)
    //=========================================================================

    assert(runFloatCompare(Opcode::FCmpOrd, 1.0, 2.0) == true);
    assert(runFloatCompare(Opcode::FCmpOrd, inf, negInf) == true);
    assert(runFloatCompare(Opcode::FCmpOrd, nan, 1.0) == false);
    assert(runFloatCompare(Opcode::FCmpOrd, 1.0, nan) == false);
    assert(runFloatCompare(Opcode::FCmpOrd, nan, nan) == false);

    //=========================================================================
    // FCmpUno tests (unordered: true if either operand is NaN)
    //=========================================================================

    assert(runFloatCompare(Opcode::FCmpUno, nan, 1.0) == true);
    assert(runFloatCompare(Opcode::FCmpUno, 1.0, nan) == true);
    assert(runFloatCompare(Opcode::FCmpUno, nan, nan) == true);
    assert(runFloatCompare(Opcode::FCmpUno, 1.0, 2.0) == false);
    assert(runFloatCompare(Opcode::FCmpUno, inf, negInf) == false);

    return 0;
}
