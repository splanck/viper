//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/FloatArithTests.cpp
// Purpose: Validate VM handlers for floating-point arithmetic opcodes
//          including NaN propagation, infinity handling, and denormals.
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

// Helper to convert int64_t back to double
double bitsToDouble(int64_t bits)
{
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

void buildFloatBinaryFunction(Module &module, Opcode op, double lhs, double rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
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

    // Perform the operation
    Instr opInstr;
    opInstr.result = builder.reserveTempId();
    opInstr.op = op;
    opInstr.type = Type(Type::Kind::F64);
    opInstr.operands.push_back(Value::temp(*lhsInstr.result));
    opInstr.operands.push_back(Value::temp(*rhsInstr.result));
    opInstr.loc = {1, 1, 1};
    bb.instructions.push_back(opInstr);

    // Return result as bits (reinterpret as i64)
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*opInstr.result));
    bb.instructions.push_back(ret);
}

double runFloatBinary(Opcode op, double lhs, double rhs)
{
    Module module;
    buildFloatBinaryFunction(module, op, lhs, rhs);
    viper::tests::VmFixture fixture;
    int64_t bits = fixture.run(module);
    return bitsToDouble(bits);
}

bool isNaN(double d)
{
    return std::isnan(d);
}

bool isInf(double d)
{
    return std::isinf(d);
}

} // namespace

int main()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double negInf = -std::numeric_limits<double>::infinity();
    const double denorm = std::numeric_limits<double>::denorm_min();

    //=========================================================================
    // FAdd tests
    //=========================================================================

    // Basic addition
    assert(runFloatBinary(Opcode::FAdd, 1.5, 2.5) == 4.0);
    assert(runFloatBinary(Opcode::FAdd, -1.0, 1.0) == 0.0);
    assert(runFloatBinary(Opcode::FAdd, 0.0, 0.0) == 0.0);

    // NaN propagation
    assert(isNaN(runFloatBinary(Opcode::FAdd, nan, 1.0)));
    assert(isNaN(runFloatBinary(Opcode::FAdd, 1.0, nan)));
    assert(isNaN(runFloatBinary(Opcode::FAdd, nan, nan)));

    // Infinity handling
    assert(runFloatBinary(Opcode::FAdd, inf, 1.0) == inf);
    assert(runFloatBinary(Opcode::FAdd, negInf, 1.0) == negInf);
    assert(isNaN(runFloatBinary(Opcode::FAdd, inf, negInf))); // inf + (-inf) = NaN

    // Denormal handling
    assert(runFloatBinary(Opcode::FAdd, denorm, 0.0) == denorm);
    assert(runFloatBinary(Opcode::FAdd, denorm, denorm) == denorm * 2);

    //=========================================================================
    // FSub tests
    //=========================================================================

    // Basic subtraction
    assert(runFloatBinary(Opcode::FSub, 5.0, 3.0) == 2.0);
    assert(runFloatBinary(Opcode::FSub, 1.0, 1.0) == 0.0);

    // NaN propagation
    assert(isNaN(runFloatBinary(Opcode::FSub, nan, 1.0)));
    assert(isNaN(runFloatBinary(Opcode::FSub, 1.0, nan)));

    // Infinity handling
    assert(runFloatBinary(Opcode::FSub, inf, 1.0) == inf);
    assert(isNaN(runFloatBinary(Opcode::FSub, inf, inf))); // inf - inf = NaN
    assert(runFloatBinary(Opcode::FSub, inf, negInf) == inf);

    // -0.0 semantics
    double negZero = -0.0;
    double posZero = 0.0;
    // 0.0 - 0.0 = 0.0 (positive zero in round-to-nearest mode)
    double zeroMinusZero = runFloatBinary(Opcode::FSub, posZero, posZero);
    assert(zeroMinusZero == 0.0);

    //=========================================================================
    // FMul tests
    //=========================================================================

    // Basic multiplication
    assert(runFloatBinary(Opcode::FMul, 2.0, 3.0) == 6.0);
    assert(runFloatBinary(Opcode::FMul, -2.0, 3.0) == -6.0);
    assert(runFloatBinary(Opcode::FMul, -2.0, -3.0) == 6.0);
    assert(runFloatBinary(Opcode::FMul, 0.0, 5.0) == 0.0);

    // NaN propagation
    assert(isNaN(runFloatBinary(Opcode::FMul, nan, 1.0)));
    assert(isNaN(runFloatBinary(Opcode::FMul, 1.0, nan)));

    // Infinity handling
    assert(runFloatBinary(Opcode::FMul, inf, 2.0) == inf);
    assert(runFloatBinary(Opcode::FMul, inf, -2.0) == negInf);
    assert(isNaN(runFloatBinary(Opcode::FMul, inf, 0.0))); // inf * 0 = NaN
    assert(isNaN(runFloatBinary(Opcode::FMul, 0.0, inf)));

    // Denormal handling
    assert(runFloatBinary(Opcode::FMul, denorm, 1.0) == denorm);
    assert(runFloatBinary(Opcode::FMul, denorm, 2.0) == denorm * 2);

    //=========================================================================
    // FDiv tests
    //=========================================================================

    // Basic division
    assert(runFloatBinary(Opcode::FDiv, 6.0, 2.0) == 3.0);
    assert(runFloatBinary(Opcode::FDiv, -6.0, 2.0) == -3.0);
    assert(runFloatBinary(Opcode::FDiv, 0.0, 1.0) == 0.0);

    // NaN propagation
    assert(isNaN(runFloatBinary(Opcode::FDiv, nan, 1.0)));
    assert(isNaN(runFloatBinary(Opcode::FDiv, 1.0, nan)));

    // Division by zero produces infinity
    assert(runFloatBinary(Opcode::FDiv, 1.0, 0.0) == inf);
    assert(runFloatBinary(Opcode::FDiv, -1.0, 0.0) == negInf);
    assert(isNaN(runFloatBinary(Opcode::FDiv, 0.0, 0.0))); // 0/0 = NaN

    // Infinity handling
    assert(runFloatBinary(Opcode::FDiv, inf, 2.0) == inf);
    assert(isNaN(runFloatBinary(Opcode::FDiv, inf, inf))); // inf/inf = NaN
    assert(runFloatBinary(Opcode::FDiv, 1.0, inf) == 0.0);

    return 0;
}
