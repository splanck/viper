//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/NarrowCastTests.cpp
// Purpose: Validate VM handlers for narrowing cast opcodes with overflow checking
//          (CastSiNarrowChk, CastUiNarrowChk).
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

using namespace il::core;

namespace
{
// Build function for signed narrowing cast with check
// The target type is specified by the instruction's type field
void buildCastSiNarrowChkFunction(Module &module, int64_t val, Type::Kind targetKind)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr conv;
    conv.result = builder.reserveTempId();
    conv.op = Opcode::CastSiNarrowChk;
    conv.type = Type(targetKind);
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

// Build function for unsigned narrowing cast with check
void buildCastUiNarrowChkFunction(Module &module, int64_t val, Type::Kind targetKind)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr conv;
    conv.result = builder.reserveTempId();
    conv.op = Opcode::CastUiNarrowChk;
    conv.type = Type(targetKind);
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

int64_t runCastSiNarrowChk(int64_t val, Type::Kind targetKind)
{
    Module module;
    buildCastSiNarrowChkFunction(module, val, targetKind);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

int64_t runCastUiNarrowChk(int64_t val, Type::Kind targetKind)
{
    Module module;
    buildCastUiNarrowChkFunction(module, val, targetKind);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

void expectInvalidCastTrapSi(int64_t val, Type::Kind targetKind)
{
    Module module;
    buildCastSiNarrowChkFunction(module, val, targetKind);
    viper::tests::VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    assert(out.find("InvalidCast") != std::string::npos);
}

void expectInvalidCastTrapUi(int64_t val, Type::Kind targetKind)
{
    Module module;
    buildCastUiNarrowChkFunction(module, val, targetKind);
    viper::tests::VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    assert(out.find("InvalidCast") != std::string::npos);
}

} // namespace

int main()
{
    //=========================================================================
    // CastSiNarrowChk to I32 tests
    //=========================================================================

    // Values that fit in I32
    assert(runCastSiNarrowChk(0, Type::Kind::I32) == 0);
    assert(runCastSiNarrowChk(1, Type::Kind::I32) == 1);
    assert(runCastSiNarrowChk(-1, Type::Kind::I32) == -1);
    assert(runCastSiNarrowChk(2147483647, Type::Kind::I32) == 2147483647);   // INT32_MAX
    assert(runCastSiNarrowChk(-2147483648, Type::Kind::I32) == -2147483648); // INT32_MIN

    // Values that overflow I32
    expectInvalidCastTrapSi(2147483648LL, Type::Kind::I32);     // INT32_MAX + 1
    expectInvalidCastTrapSi(-2147483649LL, Type::Kind::I32);    // INT32_MIN - 1
    expectInvalidCastTrapSi(std::numeric_limits<int64_t>::max(), Type::Kind::I32);
    expectInvalidCastTrapSi(std::numeric_limits<int64_t>::min(), Type::Kind::I32);

    //=========================================================================
    // CastSiNarrowChk to I16 tests
    //=========================================================================

    assert(runCastSiNarrowChk(0, Type::Kind::I16) == 0);
    assert(runCastSiNarrowChk(32767, Type::Kind::I16) == 32767);   // INT16_MAX
    assert(runCastSiNarrowChk(-32768, Type::Kind::I16) == -32768); // INT16_MIN

    expectInvalidCastTrapSi(32768LL, Type::Kind::I16);   // INT16_MAX + 1
    expectInvalidCastTrapSi(-32769LL, Type::Kind::I16);  // INT16_MIN - 1

    //=========================================================================
    // CastUiNarrowChk to I32 tests (unsigned narrowing)
    //=========================================================================

    // Values that fit in unsigned 32-bit
    assert(runCastUiNarrowChk(0, Type::Kind::I32) == 0);
    assert(runCastUiNarrowChk(1, Type::Kind::I32) == 1);
    assert(runCastUiNarrowChk(4294967295LL, Type::Kind::I32) == static_cast<int64_t>(4294967295LL)); // UINT32_MAX

    // Values that overflow unsigned 32-bit
    expectInvalidCastTrapUi(4294967296LL, Type::Kind::I32); // UINT32_MAX + 1
    expectInvalidCastTrapUi(-1, Type::Kind::I32);           // Negative (as unsigned is huge)

    //=========================================================================
    // CastUiNarrowChk to I16 tests
    //=========================================================================

    assert(runCastUiNarrowChk(0, Type::Kind::I16) == 0);
    assert(runCastUiNarrowChk(65535, Type::Kind::I16) == 65535); // UINT16_MAX

    expectInvalidCastTrapUi(65536LL, Type::Kind::I16);

    return 0;
}
