//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/CastConvertTests.cpp
// Purpose: Validate VM handlers for Cast* conversion opcodes
//          (CastSiToFp, CastUiToFp, CastFpToSiRteChk, CastFpToUiRteChk).
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

using namespace il::core;

namespace
{
int64_t doubleBits(double d)
{
    int64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    return bits;
}

double bitsToDouble(int64_t bits)
{
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

// CastSiToFp: signed int to float (similar to Sitofp)
void buildCastSiToFpFunction(Module &module, int64_t val)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr conv;
    conv.result = builder.reserveTempId();
    conv.op = Opcode::CastSiToFp;
    conv.type = Type(Type::Kind::F64);
    conv.operands.push_back(Value::constInt(val));
    conv.loc = {1, 1, 1};
    bb.instructions.push_back(conv);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*conv.result));
    bb.instructions.push_back(ret);
}

// CastUiToFp: unsigned int to float
void buildCastUiToFpFunction(Module &module, int64_t val)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr conv;
    conv.result = builder.reserveTempId();
    conv.op = Opcode::CastUiToFp;
    conv.type = Type(Type::Kind::F64);
    conv.operands.push_back(Value::constInt(val));
    conv.loc = {1, 1, 1};
    bb.instructions.push_back(conv);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*conv.result));
    bb.instructions.push_back(ret);
}

// CastFpToSiRteChk: float to signed int with round-to-even and range check
void buildCastFpToSiRteChkFunction(Module &module, double val)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr constF;
    constF.result = builder.reserveTempId();
    constF.op = Opcode::ConstF64;
    constF.type = Type(Type::Kind::F64);
    constF.operands.push_back(Value::constInt(doubleBits(val)));
    constF.loc = {1, 1, 1};
    bb.instructions.push_back(constF);

    Instr conv;
    conv.result = builder.reserveTempId();
    conv.op = Opcode::CastFpToSiRteChk;
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

// CastFpToUiRteChk: float to unsigned int with round-to-even and range check
void buildCastFpToUiRteChkFunction(Module &module, double val)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr constF;
    constF.result = builder.reserveTempId();
    constF.op = Opcode::ConstF64;
    constF.type = Type(Type::Kind::F64);
    constF.operands.push_back(Value::constInt(doubleBits(val)));
    constF.loc = {1, 1, 1};
    bb.instructions.push_back(constF);

    Instr conv;
    conv.result = builder.reserveTempId();
    conv.op = Opcode::CastFpToUiRteChk;
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

double runCastSiToFp(int64_t val)
{
    Module module;
    buildCastSiToFpFunction(module, val);
    viper::tests::VmFixture fixture;
    return bitsToDouble(fixture.run(module));
}

double runCastUiToFp(int64_t val)
{
    Module module;
    buildCastUiToFpFunction(module, val);
    viper::tests::VmFixture fixture;
    return bitsToDouble(fixture.run(module));
}

int64_t runCastFpToSiRteChk(double val)
{
    Module module;
    buildCastFpToSiRteChkFunction(module, val);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

int64_t runCastFpToUiRteChk(double val)
{
    Module module;
    buildCastFpToUiRteChkFunction(module, val);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

void expectInvalidCastTrap(double val, bool isSigned)
{
    Module module;
    if (isSigned)
    {
        buildCastFpToSiRteChkFunction(module, val);
    }
    else
    {
        buildCastFpToUiRteChkFunction(module, val);
    }
    viper::tests::VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    assert(out.find("InvalidCast") != std::string::npos);
}

} // namespace

int main()
{
    const int64_t maxVal = std::numeric_limits<int64_t>::max();
    const int64_t minVal = std::numeric_limits<int64_t>::min();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double negInf = -std::numeric_limits<double>::infinity();

    //=========================================================================
    // CastSiToFp tests (signed int to float)
    //=========================================================================

    assert(runCastSiToFp(0) == 0.0);
    assert(runCastSiToFp(1) == 1.0);
    assert(runCastSiToFp(-1) == -1.0);
    assert(runCastSiToFp(42) == 42.0);
    assert(runCastSiToFp(-42) == -42.0);

    // Large values
    double maxAsDouble = runCastSiToFp(maxVal);
    assert(maxAsDouble > 0);
    double minAsDouble = runCastSiToFp(minVal);
    assert(minAsDouble < 0);

    //=========================================================================
    // CastUiToFp tests (unsigned int to float)
    //=========================================================================

    assert(runCastUiToFp(0) == 0.0);
    assert(runCastUiToFp(1) == 1.0);
    assert(runCastUiToFp(42) == 42.0);

    // -1 as unsigned is MAX_UINT64
    double negOneAsUnsigned = runCastUiToFp(-1);
    assert(negOneAsUnsigned > 0);

    //=========================================================================
    // CastFpToSiRteChk tests (float to signed int with rounding and range check)
    //=========================================================================

    // Basic conversions
    assert(runCastFpToSiRteChk(0.0) == 0);
    assert(runCastFpToSiRteChk(1.0) == 1);
    assert(runCastFpToSiRteChk(-1.0) == -1);
    assert(runCastFpToSiRteChk(42.0) == 42);
    assert(runCastFpToSiRteChk(-42.0) == -42);

    // Round-to-even behavior (banker's rounding)
    // 0.5 rounds to 0 (even)
    assert(runCastFpToSiRteChk(0.5) == 0);
    // 1.5 rounds to 2 (even)
    assert(runCastFpToSiRteChk(1.5) == 2);
    // 2.5 rounds to 2 (even)
    assert(runCastFpToSiRteChk(2.5) == 2);
    // 3.5 rounds to 4 (even)
    assert(runCastFpToSiRteChk(3.5) == 4);
    // -0.5 rounds to 0 (even)
    assert(runCastFpToSiRteChk(-0.5) == 0);
    // -1.5 rounds to -2 (even)
    assert(runCastFpToSiRteChk(-1.5) == -2);

    // Values not exactly at .5 round normally
    assert(runCastFpToSiRteChk(0.4) == 0);
    assert(runCastFpToSiRteChk(0.6) == 1);
    assert(runCastFpToSiRteChk(-0.4) == 0);
    assert(runCastFpToSiRteChk(-0.6) == -1);

    // Out of range values should trap
    expectInvalidCastTrap(nan, true);
    expectInvalidCastTrap(inf, true);
    expectInvalidCastTrap(negInf, true);

    //=========================================================================
    // CastFpToUiRteChk tests (float to unsigned int with rounding and range check)
    //=========================================================================

    assert(runCastFpToUiRteChk(0.0) == 0);
    assert(runCastFpToUiRteChk(1.0) == 1);
    assert(runCastFpToUiRteChk(42.0) == 42);

    // Round-to-even
    assert(runCastFpToUiRteChk(0.5) == 0);
    assert(runCastFpToUiRteChk(1.5) == 2);
    assert(runCastFpToUiRteChk(2.5) == 2);

    // Negative values should trap
    expectInvalidCastTrap(-1.0, false);
    expectInvalidCastTrap(-0.6, false);

    // Special values should trap
    expectInvalidCastTrap(nan, false);
    expectInvalidCastTrap(inf, false);
    expectInvalidCastTrap(negInf, false);

    return 0;
}
