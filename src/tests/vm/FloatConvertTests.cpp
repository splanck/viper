//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/FloatConvertTests.cpp
// Purpose: Validate VM handlers for float/int conversion opcodes
//          (Sitofp, Fptosi).
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

// Build function: int64 -> double (Sitofp)
void buildSitofpFunction(Module &module, int64_t val)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // Convert int to float
    Instr conv;
    conv.result = builder.reserveTempId();
    conv.op = Opcode::Sitofp;
    conv.type = Type(Type::Kind::F64);
    conv.operands.push_back(Value::constInt(val));
    conv.loc = {1, 1, 1};
    bb.instructions.push_back(conv);

    // Return as bits
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*conv.result));
    bb.instructions.push_back(ret);
}

// Build function: double -> int64 (Fptosi)
void buildFptosiFunction(Module &module, double val)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // Create float constant
    Instr constF;
    constF.result = builder.reserveTempId();
    constF.op = Opcode::ConstF64;
    constF.type = Type(Type::Kind::F64);
    constF.operands.push_back(Value::constInt(doubleBits(val)));
    constF.loc = {1, 1, 1};
    bb.instructions.push_back(constF);

    // Convert to int
    Instr conv;
    conv.result = builder.reserveTempId();
    conv.op = Opcode::Fptosi;
    conv.type = Type(Type::Kind::I64);
    conv.operands.push_back(Value::temp(*constF.result));
    conv.loc = {1, 1, 1};
    bb.instructions.push_back(conv);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*conv.result));
    bb.instructions.push_back(ret);
}

double runSitofp(int64_t val)
{
    Module module;
    buildSitofpFunction(module, val);
    viper::tests::VmFixture fixture;
    return bitsToDouble(fixture.run(module));
}

int64_t runFptosi(double val)
{
    Module module;
    buildFptosiFunction(module, val);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    //=========================================================================
    // Sitofp tests (signed int to float)
    //=========================================================================

    assert(runSitofp(0) == 0.0);
    assert(runSitofp(1) == 1.0);
    assert(runSitofp(-1) == -1.0);
    assert(runSitofp(42) == 42.0);
    assert(runSitofp(-42) == -42.0);

    // Large values (may lose precision)
    const int64_t maxVal = std::numeric_limits<int64_t>::max();
    const int64_t minVal = std::numeric_limits<int64_t>::min();
    double maxAsDouble = runSitofp(maxVal);
    assert(maxAsDouble > 0);
    double minAsDouble = runSitofp(minVal);
    assert(minAsDouble < 0);

    //=========================================================================
    // Fptosi tests (float to signed int)
    //=========================================================================

    assert(runFptosi(0.0) == 0);
    assert(runFptosi(1.0) == 1);
    assert(runFptosi(-1.0) == -1);
    assert(runFptosi(42.5) == 42);   // Truncation
    assert(runFptosi(-42.5) == -42); // Truncation toward zero
    assert(runFptosi(42.9) == 42);
    assert(runFptosi(-42.9) == -42);

    return 0;
}
