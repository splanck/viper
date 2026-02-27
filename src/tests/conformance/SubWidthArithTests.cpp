//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/conformance/SubWidthArithTests.cpp
// Purpose: Conformance tests for checked arithmetic and division at I32 and
//          I16 type widths. Plain Add/Sub/Mul are I64-only; sub-width typing
//          affects checked ops (IAddOvf, SDivChk0, etc.) which use the type
//          width for overflow/range detection.
//
// Complements: IntOverflowTests.cpp (I64 overflow checks),
//              IntUnsignedOpsTests.cpp (I16 overflow, some I32 div/rem).
//
// Semantics (see docs/arithmetic-semantics.md):
//   - IAddOvf/ISubOvf/IMulOvf: Trap when result exceeds type's signed range.
//   - SDivChk0: Traps on div-by-zero AND MIN/-1 at type width.
//   - SRemChk0: Traps on div-by-zero. MIN%-1 = 0 (no trap).
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

void buildBinaryFunction(Module &module, Opcode op, Type::Kind type,
                         int64_t lhs, int64_t rhs)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr instr;
    instr.result = builder.reserveTempId();
    instr.op = op;
    instr.type = Type(type);
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

int64_t runBinary(Opcode op, Type::Kind type, int64_t lhs, int64_t rhs)
{
    Module module;
    buildBinaryFunction(module, op, type, lhs, rhs);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

void expectTrap(Opcode op, Type::Kind type, int64_t lhs, int64_t rhs,
                const char *trapKind)
{
    Module module;
    buildBinaryFunction(module, op, type, lhs, rhs);
    viper::tests::VmFixture fixture;
    const std::string out = fixture.captureTrap(module);
    assert(out.find(trapKind) != std::string::npos);
}

} // namespace

int main()
{
    //=========================================================================
    // I32 Checked Overflow (IAddOvf, ISubOvf, IMulOvf at i32)
    //=========================================================================

    constexpr int64_t i32Max = std::numeric_limits<int32_t>::max();  // 2147483647
    constexpr int64_t i32Min = std::numeric_limits<int32_t>::min();  // -2147483648

    // Non-overflowing I32 addition
    assert(runBinary(Opcode::IAddOvf, Type::Kind::I32, 1, 2) == 3);
    assert(runBinary(Opcode::IAddOvf, Type::Kind::I32, -1, 1) == 0);
    assert(runBinary(Opcode::IAddOvf, Type::Kind::I32, i32Max - 1, 1) == i32Max);
    assert(runBinary(Opcode::IAddOvf, Type::Kind::I32, i32Min + 1, -1) == i32Min);

    // Overflowing I32 addition — traps
    expectTrap(Opcode::IAddOvf, Type::Kind::I32, i32Max, 1, "Overflow");
    expectTrap(Opcode::IAddOvf, Type::Kind::I32, i32Min, -1, "Overflow");
    expectTrap(Opcode::IAddOvf, Type::Kind::I32, i32Max, i32Max, "Overflow");

    // Non-overflowing I32 subtraction
    assert(runBinary(Opcode::ISubOvf, Type::Kind::I32, 5, 3) == 2);
    assert(runBinary(Opcode::ISubOvf, Type::Kind::I32, i32Min + 1, 1) == i32Min);

    // Overflowing I32 subtraction — traps
    expectTrap(Opcode::ISubOvf, Type::Kind::I32, i32Min, 1, "Overflow");
    expectTrap(Opcode::ISubOvf, Type::Kind::I32, i32Max, -1, "Overflow");

    // Non-overflowing I32 multiplication
    assert(runBinary(Opcode::IMulOvf, Type::Kind::I32, 2, 3) == 6);
    assert(runBinary(Opcode::IMulOvf, Type::Kind::I32, -2, 3) == -6);
    assert(runBinary(Opcode::IMulOvf, Type::Kind::I32, 0, i32Max) == 0);

    // Overflowing I32 multiplication — traps
    expectTrap(Opcode::IMulOvf, Type::Kind::I32, i32Max, 2, "Overflow");
    expectTrap(Opcode::IMulOvf, Type::Kind::I32, i32Min, 2, "Overflow");
    expectTrap(Opcode::IMulOvf, Type::Kind::I32, -1, i32Min, "Overflow");

    //=========================================================================
    // I32 Division and Remainder
    //=========================================================================

    // Signed division truncation toward zero (C99)
    assert(runBinary(Opcode::SDivChk0, Type::Kind::I32, 7, -2) == -3);
    assert(runBinary(Opcode::SDivChk0, Type::Kind::I32, -7, 2) == -3);
    assert(runBinary(Opcode::SDivChk0, Type::Kind::I32, -7, -2) == 3);
    assert(runBinary(Opcode::SDivChk0, Type::Kind::I32, 7, 2) == 3);

    // I32 MIN/-1 traps
    expectTrap(Opcode::SDivChk0, Type::Kind::I32, i32Min, -1, "Overflow");

    // I32 divide by zero traps
    expectTrap(Opcode::SDivChk0, Type::Kind::I32, 42, 0, "DivideByZero");

    // Signed remainder — dividend sign rule
    assert(runBinary(Opcode::SRemChk0, Type::Kind::I32, -7, 2) == -1);
    assert(runBinary(Opcode::SRemChk0, Type::Kind::I32, 7, -2) == 1);
    assert(runBinary(Opcode::SRemChk0, Type::Kind::I32, -7, -2) == -1);
    assert(runBinary(Opcode::SRemChk0, Type::Kind::I32, 7, 2) == 1);

    // I32 MIN % -1 = 0 (no trap)
    assert(runBinary(Opcode::SRemChk0, Type::Kind::I32, i32Min, -1) == 0);

    // I32 rem divide by zero traps
    expectTrap(Opcode::SRemChk0, Type::Kind::I32, 42, 0, "DivideByZero");

    //=========================================================================
    // I16 Checked Overflow
    //=========================================================================

    constexpr int64_t i16Max = std::numeric_limits<int16_t>::max();  // 32767
    constexpr int64_t i16Min = std::numeric_limits<int16_t>::min();  // -32768

    // Non-overflowing I16
    assert(runBinary(Opcode::IAddOvf, Type::Kind::I16, 100, 200) == 300);
    assert(runBinary(Opcode::ISubOvf, Type::Kind::I16, 200, 100) == 100);
    assert(runBinary(Opcode::IMulOvf, Type::Kind::I16, 10, 20) == 200);

    // Overflowing I16 — traps
    expectTrap(Opcode::IAddOvf, Type::Kind::I16, i16Max, 1, "Overflow");
    expectTrap(Opcode::ISubOvf, Type::Kind::I16, i16Min, 1, "Overflow");
    expectTrap(Opcode::IMulOvf, Type::Kind::I16, i16Max, 2, "Overflow");

    //=========================================================================
    // I16 Division and Remainder
    //=========================================================================

    // Truncation toward zero
    assert(runBinary(Opcode::SDivChk0, Type::Kind::I16, -7, 2) == -3);

    // I16 MIN/-1 traps
    expectTrap(Opcode::SDivChk0, Type::Kind::I16, i16Min, -1, "Overflow");

    // I16 MIN % -1 = 0
    assert(runBinary(Opcode::SRemChk0, Type::Kind::I16, i16Min, -1) == 0);

    //=========================================================================
    // Unsigned Division at I64
    //=========================================================================

    // Unsigned division treats -1 as UINT64_MAX
    assert(runBinary(Opcode::UDivChk0, Type::Kind::I64, -1, 2) ==
           std::numeric_limits<int64_t>::max());

    // Unsigned remainder
    assert(runBinary(Opcode::URemChk0, Type::Kind::I64, -1, 2) == 1);

    // Unsigned divide by zero traps
    expectTrap(Opcode::UDivChk0, Type::Kind::I64, 42, 0, "DivideByZero");
    expectTrap(Opcode::URemChk0, Type::Kind::I64, 42, 0, "DivideByZero");

    return 0;
}
