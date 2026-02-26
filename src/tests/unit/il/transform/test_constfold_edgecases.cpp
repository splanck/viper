//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_constfold_edgecases.cpp
// Purpose: Tests for constant folding edge cases — division by zero, signed
//          overflow, shift-by-bitwidth, floating-point specials, and normal
//          arithmetic that should fold correctly.
// Key invariants: Operations that would trap at runtime must NOT be folded.
//                 Operations that are well-defined must fold to the correct
//                 constant value.
// Ownership/Lifetime: Builds transient modules per test invocation.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include "tests/TestHarness.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

using namespace il::core;

namespace
{

static void verifyOrDie(const Module &module)
{
    auto verifyResult = il::verify::Verifier::verify(module);
    if (!verifyResult)
    {
        il::support::printDiag(verifyResult.error(), std::cerr);
        ASSERT_TRUE(false && "Module verification failed");
    }
}

void emitBinOp(BasicBlock &bb,
               Opcode op,
               Value lhs,
               Value rhs,
               unsigned resultId,
               Type ty = Type(Type::Kind::I64))
{
    Instr instr;
    instr.op = op;
    instr.result = resultId;
    instr.type = ty;
    instr.operands.push_back(lhs);
    instr.operands.push_back(rhs);
    bb.instructions.push_back(instr);
}

/// Build a minimal module: one function with one block containing a single
/// binary operation whose result is returned.
Module buildConstFoldTest(Opcode op, Value lhs, Value rhs, Type ty = Type(Type::Kind::I64))
{
    Module module;
    Function fn;
    fn.name = "test";
    fn.retType = ty;

    BasicBlock entry;
    entry.label = "entry";

    unsigned resultId = 0;
    emitBinOp(entry, op, lhs, rhs, resultId, ty);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(resultId));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(1);
    fn.valueNames[0] = "result";
    module.functions.push_back(std::move(fn));
    return module;
}

/// Check if the Ret operand was folded to a specific integer constant.
bool retFoldedToInt(const Module &module, int64_t expected)
{
    const auto &ret = module.functions[0].blocks[0].instructions.back();
    if (ret.op != Opcode::Ret || ret.operands.empty())
        return false;
    const auto &v = ret.operands[0];
    return v.kind == Value::Kind::ConstInt && v.i64 == expected;
}

/// Check if the Ret operand is still a temp reference (not folded).
bool retNotFolded(const Module &module)
{
    const auto &ret = module.functions[0].blocks[0].instructions.back();
    if (ret.op != Opcode::Ret || ret.operands.empty())
        return false;
    return ret.operands[0].kind == Value::Kind::Temp;
}

} // namespace

// ---------------------------------------------------------------------------
// Division-by-zero edge cases: must NOT fold (would trap at runtime)
// ---------------------------------------------------------------------------

TEST(ConstFoldEdge, SDivChk0_DivByZero)
{
    auto module = buildConstFoldTest(
        Opcode::SDivChk0, Value::constInt(42), Value::constInt(0));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, UDivChk0_DivByZero)
{
    auto module = buildConstFoldTest(
        Opcode::UDivChk0, Value::constInt(42), Value::constInt(0));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, SRemChk0_DivByZero)
{
    auto module = buildConstFoldTest(
        Opcode::SRemChk0, Value::constInt(42), Value::constInt(0));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

// ---------------------------------------------------------------------------
// Signed overflow edge cases: must NOT fold (would trap at runtime)
// ---------------------------------------------------------------------------

TEST(ConstFoldEdge, SDivChk0_MinDivNeg1)
{
    // INT64_MIN / -1 overflows in two's complement
    auto module = buildConstFoldTest(
        Opcode::SDivChk0,
        Value::constInt(std::numeric_limits<int64_t>::min()),
        Value::constInt(-1));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, IAddOvf_Overflow)
{
    // INT64_MAX + 1 overflows
    auto module = buildConstFoldTest(
        Opcode::IAddOvf,
        Value::constInt(std::numeric_limits<int64_t>::max()),
        Value::constInt(1));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, ISubOvf_Underflow)
{
    // INT64_MIN - 1 underflows
    auto module = buildConstFoldTest(
        Opcode::ISubOvf,
        Value::constInt(std::numeric_limits<int64_t>::min()),
        Value::constInt(1));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, IMulOvf_Overflow)
{
    // INT64_MAX * 2 overflows
    auto module = buildConstFoldTest(
        Opcode::IMulOvf,
        Value::constInt(std::numeric_limits<int64_t>::max()),
        Value::constInt(2));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, IMulOvf_MinTimesNeg1)
{
    // INT64_MIN * -1 overflows (result would be INT64_MAX + 1)
    auto module = buildConstFoldTest(
        Opcode::IMulOvf,
        Value::constInt(std::numeric_limits<int64_t>::min()),
        Value::constInt(-1));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

// ---------------------------------------------------------------------------
// Overflow-checked arithmetic that does NOT overflow: must fold
// ---------------------------------------------------------------------------

TEST(ConstFoldEdge, IAddOvf_NoOverflow)
{
    // INT64_MAX + 0 does not overflow
    auto module = buildConstFoldTest(
        Opcode::IAddOvf,
        Value::constInt(std::numeric_limits<int64_t>::max()),
        Value::constInt(0));
    il::transform::constFold(module);
    ASSERT_TRUE(retFoldedToInt(module, std::numeric_limits<int64_t>::max()));
}

// ---------------------------------------------------------------------------
// Floating-point edge cases
// ---------------------------------------------------------------------------

TEST(ConstFoldEdge, FDiv_ByZero)
{
    // 1.0 / 0.0 => inf — constfolder must NOT fold (guards against non-finite results)
    auto module = buildConstFoldTest(
        Opcode::FDiv,
        Value::constFloat(1.0),
        Value::constFloat(0.0),
        Type(Type::Kind::F64));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, FMul_InfTimesZero)
{
    // INF * 0.0 => NaN — constfolder must NOT fold (non-finite result)
    auto module = buildConstFoldTest(
        Opcode::FMul,
        Value::constFloat(std::numeric_limits<double>::infinity()),
        Value::constFloat(0.0),
        Type(Type::Kind::F64));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, FAdd_InfPlusInf)
{
    // INF + INF = INF, which is well-defined but non-finite.
    // The constfolder skips non-finite results, so this should NOT fold.
    // At minimum, verify the module is valid after the pass runs.
    auto module = buildConstFoldTest(
        Opcode::FAdd,
        Value::constFloat(std::numeric_limits<double>::infinity()),
        Value::constFloat(std::numeric_limits<double>::infinity()),
        Type(Type::Kind::F64));
    il::transform::constFold(module);
    // The constfolder refuses non-finite results, so the temp should remain.
    ASSERT_TRUE(retNotFolded(module));
}

// ---------------------------------------------------------------------------
// Normal arithmetic: must fold to the correct constant
// ---------------------------------------------------------------------------

TEST(ConstFoldEdge, NormalArithmetic)
{
    // iadd.ovf 3, 4 => 7
    auto module = buildConstFoldTest(
        Opcode::IAddOvf, Value::constInt(3), Value::constInt(4));
    il::transform::constFold(module);
    ASSERT_TRUE(retFoldedToInt(module, 7));
}

TEST(ConstFoldEdge, NormalComparison)
{
    // scmp.lt 5, 10 => 1 (true)
    auto module = buildConstFoldTest(
        Opcode::SCmpLT,
        Value::constInt(5),
        Value::constInt(10),
        Type(Type::Kind::I1));
    il::transform::constFold(module);

    const auto &ret = module.functions[0].blocks[0].instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_EQ(ret.operands.size(), 1);
    ASSERT_EQ(ret.operands[0].kind, Value::Kind::ConstInt);
    ASSERT_TRUE(ret.operands[0].isBool);
    ASSERT_EQ(ret.operands[0].i64, 1);
}

// ---------------------------------------------------------------------------
// Shift edge cases
// ---------------------------------------------------------------------------

TEST(ConstFoldEdge, Shl_ByBitwidth)
{
    // shl 1, 64 => undefined behavior — must NOT fold
    auto module = buildConstFoldTest(
        Opcode::Shl, Value::constInt(1), Value::constInt(64));
    il::transform::constFold(module);
    ASSERT_TRUE(retNotFolded(module));
}

TEST(ConstFoldEdge, Shl_Normal)
{
    // shl 1, 3 => 8
    auto module = buildConstFoldTest(
        Opcode::Shl, Value::constInt(1), Value::constInt(3));
    il::transform::constFold(module);
    ASSERT_TRUE(retFoldedToInt(module, 8));
}

TEST(ConstFoldEdge, LShr_Normal)
{
    // lshr 16, 2 => 4
    auto module = buildConstFoldTest(
        Opcode::LShr, Value::constInt(16), Value::constInt(2));
    il::transform::constFold(module);
    ASSERT_TRUE(retFoldedToInt(module, 4));
}

// ---------------------------------------------------------------------------
// Non-overflow integer arithmetic: must fold
// ---------------------------------------------------------------------------

TEST(ConstFoldEdge, IMul_Normal)
{
    // imul.ovf 6, 7 => 42
    auto module = buildConstFoldTest(
        Opcode::IMulOvf, Value::constInt(6), Value::constInt(7));
    il::transform::constFold(module);
    ASSERT_TRUE(retFoldedToInt(module, 42));
}

TEST(ConstFoldEdge, ISub_Normal)
{
    // isub.ovf 10, 3 => 7
    auto module = buildConstFoldTest(
        Opcode::ISubOvf, Value::constInt(10), Value::constInt(3));
    il::transform::constFold(module);
    ASSERT_TRUE(retFoldedToInt(module, 7));
}

TEST(ConstFoldEdge, SDivChk0_Normal)
{
    // sdiv.chk0 42, 7 => 6
    auto module = buildConstFoldTest(
        Opcode::SDivChk0, Value::constInt(42), Value::constInt(7));
    il::transform::constFold(module);
    ASSERT_TRUE(retFoldedToInt(module, 6));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
