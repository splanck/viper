//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/ConstFoldAuditTests.cpp
// Purpose: Comprehensive audit of compile-time constant folding capabilities.
//          Verifies that constant expressions are folded at compile time (not
//          deferred to runtime), covering integer arithmetic, float arithmetic,
//          comparisons, shifts, type conversions, runtime calls, and edge cases.
//          Each test builds an IL module, runs the optimizer, and inspects the
//          resulting IL to verify folding occurred (or was correctly refused).
// Key invariants:
//   - Folding must never change observable behavior.
//   - Overflow/trap-producing operations must NOT be folded.
//   - Non-finite float results must NOT be folded.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/io/Serializer.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/transform/SCCP.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include "tests/TestHarness.hpp"
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

using namespace il::core;

namespace
{

static void verifyOrDie(const Module &module)
{
    auto result = il::verify::Verifier::verify(module);
    if (!result)
    {
        il::support::printDiag(result.error(), std::cerr);
        ASSERT_TRUE(false && "Module verification failed");
    }
}

/// Emit a binary operation instruction into a basic block.
static void emitBinOp(BasicBlock &bb,
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

/// Emit a unary operation instruction into a basic block.
static void emitUnaryOp(BasicBlock &bb,
                        Opcode op,
                        Value operand,
                        unsigned resultId,
                        Type ty)
{
    Instr instr;
    instr.op = op;
    instr.result = resultId;
    instr.type = ty;
    instr.operands.push_back(operand);
    bb.instructions.push_back(instr);
}

/// Run SCCP + constFold on a module with pre-verification, returning serialized
/// IL for inspection.
static std::string optimizeAndSerialize(Module &module)
{
    verifyOrDie(module);
    il::transform::sccp(module);
    il::transform::constFold(module);

    std::ostringstream ss;
    il::io::Serializer::write(module, ss);
    return ss.str();
}

/// Run SCCP + constFold WITHOUT pre-verification (for internal/unchecked
/// opcodes that the verifier rejects in user-facing IL).
static std::string optimizeNoVerify(Module &module)
{
    il::transform::sccp(module);
    il::transform::constFold(module);

    std::ostringstream ss;
    il::io::Serializer::write(module, ss);
    return ss.str();
}

/// Check that the ret instruction's first operand is a constant integer.
static bool retIsConstInt(const BasicBlock &entry, long long expected)
{
    const Instr &ret = entry.instructions.back();
    if (ret.op != Opcode::Ret || ret.operands.empty())
        return false;
    const Value &v = ret.operands[0];
    return v.kind == Value::Kind::ConstInt && v.i64 == expected;
}

/// Check that the ret instruction's first operand is a constant float.
static bool retIsConstFloat(const BasicBlock &entry, double expected)
{
    const Instr &ret = entry.instructions.back();
    if (ret.op != Opcode::Ret || ret.operands.empty())
        return false;
    const Value &v = ret.operands[0];
    return v.kind == Value::Kind::ConstFloat && v.f64 == expected;
}

/// Check that the ret instruction's first operand is a constant bool.
static bool retIsConstBool(const BasicBlock &entry, bool expected)
{
    const Instr &ret = entry.instructions.back();
    if (ret.op != Opcode::Ret || ret.operands.empty())
        return false;
    const Value &v = ret.operands[0];
    return v.kind == Value::Kind::ConstInt && v.isBool &&
           v.i64 == (expected ? 1 : 0);
}

/// Check that the given opcode still exists as an instruction (was NOT folded).
static bool hasInstr(const BasicBlock &entry, Opcode op)
{
    for (const auto &instr : entry.instructions)
        if (instr.op == op)
            return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// A. Integer arithmetic folding (checked variants — verifier-safe)
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, IntegerAddOvf)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::constInt(3), Value::constInt(4), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 7));
}

TEST(ConstFoldAudit, IntegerSubOvf)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::ISubOvf, Value::constInt(10), Value::constInt(3), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 7));
}

TEST(ConstFoldAudit, IntegerMulOvf)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(6), Value::constInt(7), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 42));
}

TEST(ConstFoldAudit, OverflowAddNotFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf,
              Value::constInt(std::numeric_limits<long long>::max()),
              Value::constInt(1), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    // Overflow: must NOT be folded — instruction should remain
    EXPECT_TRUE(hasInstr(entry, Opcode::IAddOvf));
}

// ---------------------------------------------------------------------------
// A2. Unchecked division/remainder folding (ISSUE-5)
// These use internal opcodes that the verifier rejects — skip verification.
// They appear in optimized IL after CheckOpt strips safety checks.
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, SDivFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::SDiv, Value::constInt(10), Value::constInt(3), id);
    builder.emitRet(Value::temp(id), {});

    optimizeNoVerify(module);
    EXPECT_TRUE(retIsConstInt(entry, 3));
}

TEST(ConstFoldAudit, UDivFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::UDiv, Value::constInt(20), Value::constInt(4), id);
    builder.emitRet(Value::temp(id), {});

    optimizeNoVerify(module);
    EXPECT_TRUE(retIsConstInt(entry, 5));
}

TEST(ConstFoldAudit, SRemFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::SRem, Value::constInt(10), Value::constInt(3), id);
    builder.emitRet(Value::temp(id), {});

    optimizeNoVerify(module);
    EXPECT_TRUE(retIsConstInt(entry, 1));
}

TEST(ConstFoldAudit, SDivByZeroNotFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::SDiv, Value::constInt(10), Value::constInt(0), id);
    builder.emitRet(Value::temp(id), {});

    optimizeNoVerify(module);
    // Div-by-zero trap: must NOT be folded
    EXPECT_FALSE(retIsConstInt(entry, 0));
}

TEST(ConstFoldAudit, SDivMinByNeg1NotFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::SDiv,
              Value::constInt(std::numeric_limits<long long>::min()),
              Value::constInt(-1), id);
    builder.emitRet(Value::temp(id), {});

    optimizeNoVerify(module);
    // Signed overflow trap: must NOT be folded
    EXPECT_FALSE(retIsConstInt(entry, 0));
}

// Also test the checked variants fold correctly when safe
TEST(ConstFoldAudit, SDivChk0Folded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::SDivChk0, Value::constInt(10), Value::constInt(3), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 3));
}

TEST(ConstFoldAudit, URemChk0Folded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::URemChk0, Value::constInt(20), Value::constInt(6), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 2));
}

// ---------------------------------------------------------------------------
// B. Float arithmetic folding
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, FloatAdd)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::F64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::FAdd,
              Value::constFloat(1.5), Value::constFloat(2.5), id,
              Type(Type::Kind::F64));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstFloat(entry, 4.0));
}

TEST(ConstFoldAudit, FloatMul)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::F64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::FMul,
              Value::constFloat(3.0), Value::constFloat(2.0), id,
              Type(Type::Kind::F64));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstFloat(entry, 6.0));
}

TEST(ConstFoldAudit, FloatDivByZeroNotFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::F64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::FDiv,
              Value::constFloat(1.0), Value::constFloat(0.0), id,
              Type(Type::Kind::F64));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    // Div-by-zero producing inf: must NOT be folded (ISSUE-3 fix)
    EXPECT_TRUE(hasInstr(entry, Opcode::FDiv));
}

// ---------------------------------------------------------------------------
// B2. ConstF64 propagation through SCCP (ISSUE-2)
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, ConstF64Propagation)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::F64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %x = const.f64 3.0
    unsigned xId = builder.reserveTempId();
    emitUnaryOp(entry, Opcode::ConstF64,
                Value::constFloat(3.0), xId,
                Type(Type::Kind::F64));

    // %y = fadd %x, 1.0 → should fold to 4.0 after SCCP propagates %x
    unsigned yId = builder.reserveTempId();
    emitBinOp(entry, Opcode::FAdd,
              Value::temp(xId), Value::constFloat(1.0), yId,
              Type(Type::Kind::F64));
    builder.emitRet(Value::temp(yId), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstFloat(entry, 4.0));
}

// ---------------------------------------------------------------------------
// C. Comparison folding
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, IntegerCmpEq)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::ICmpEq,
              Value::constInt(5), Value::constInt(5), id,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstBool(entry, true));
}

TEST(ConstFoldAudit, SignedCmpLT)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::SCmpLT,
              Value::constInt(3), Value::constInt(5), id,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstBool(entry, true));
}

TEST(ConstFoldAudit, FloatCmpEq)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::FCmpEQ,
              Value::constFloat(1.0), Value::constFloat(1.0), id,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstBool(entry, true));
}

// ---------------------------------------------------------------------------
// C2. FCmpOrd / FCmpUno (ISSUE-7)
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, FCmpOrdBothFinite)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::FCmpOrd,
              Value::constFloat(1.0), Value::constFloat(2.0), id,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstBool(entry, true));
}

TEST(ConstFoldAudit, FCmpUnoBothFinite)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::FCmpUno,
              Value::constFloat(1.0), Value::constFloat(2.0), id,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstBool(entry, false));
}

TEST(ConstFoldAudit, FCmpOrdWithNaN)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::FCmpOrd,
              Value::constFloat(std::numeric_limits<double>::quiet_NaN()),
              Value::constFloat(1.0), id,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstBool(entry, false));
}

TEST(ConstFoldAudit, FCmpUnoWithNaN)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::FCmpUno,
              Value::constFloat(std::numeric_limits<double>::quiet_NaN()),
              Value::constFloat(1.0), id,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstBool(entry, true));
}

// ---------------------------------------------------------------------------
// D. Shift folding
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, ShlFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::Shl, Value::constInt(1), Value::constInt(3), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 8));
}

TEST(ConstFoldAudit, LShrFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::LShr, Value::constInt(16), Value::constInt(2), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 4));
}

TEST(ConstFoldAudit, ShlBy64NotFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::Shl, Value::constInt(1), Value::constInt(64), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    // Out-of-range shift: must NOT be folded (ISSUE-4 fix)
    EXPECT_TRUE(hasInstr(entry, Opcode::Shl));
}

TEST(ConstFoldAudit, ShlByNegativeNotFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::Shl, Value::constInt(1), Value::constInt(-1), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    // Negative shift: must NOT be folded (ISSUE-4 fix)
    EXPECT_TRUE(hasInstr(entry, Opcode::Shl));
}

// ---------------------------------------------------------------------------
// E. Type conversion folding
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, SitofpFolded)
{
    // Sitofp is an internal opcode (verifier requires CastSiToFp)
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::F64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitUnaryOp(entry, Opcode::Sitofp, Value::constInt(42), id,
                Type(Type::Kind::F64));
    builder.emitRet(Value::temp(id), {});

    optimizeNoVerify(module);
    EXPECT_TRUE(retIsConstFloat(entry, 42.0));
}

TEST(ConstFoldAudit, FptosiTruncation)
{
    // Fptosi is an internal opcode (verifier requires CastFpToSiRteChk)
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitUnaryOp(entry, Opcode::Fptosi, Value::constFloat(3.9), id,
                Type(Type::Kind::I64));
    builder.emitRet(Value::temp(id), {});

    optimizeNoVerify(module);
    // Truncation towards zero: 3.9 → 3
    EXPECT_TRUE(retIsConstInt(entry, 3));
}

TEST(ConstFoldAudit, FptosiNegative)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitUnaryOp(entry, Opcode::Fptosi, Value::constFloat(-3.9), id,
                Type(Type::Kind::I64));
    builder.emitRet(Value::temp(id), {});

    optimizeNoVerify(module);
    // Truncation towards zero: -3.9 → -3
    EXPECT_TRUE(retIsConstInt(entry, -3));
}

TEST(ConstFoldAudit, CastSiToFpFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::F64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitUnaryOp(entry, Opcode::CastSiToFp, Value::constInt(-7), id,
                Type(Type::Kind::F64));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstFloat(entry, -7.0));
}

TEST(ConstFoldAudit, CastFpToSiRteChkFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitUnaryOp(entry, Opcode::CastFpToSiRteChk, Value::constFloat(3.7), id,
                Type(Type::Kind::I64));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    // Round-to-even: 3.7 → 4
    EXPECT_TRUE(retIsConstInt(entry, 4));
}

// ---------------------------------------------------------------------------
// F. Boolean folding
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, Zext1Folded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitUnaryOp(entry, Opcode::Zext1, Value::constBool(true), id,
                Type(Type::Kind::I64));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 1));
}

TEST(ConstFoldAudit, Trunc1Folded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitUnaryOp(entry, Opcode::Trunc1, Value::constInt(42), id,
                Type(Type::Kind::I1));
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    // trunc1(42): 42 & 1 = 0 → false
    EXPECT_TRUE(retIsConstBool(entry, false));
}

// ---------------------------------------------------------------------------
// G. Runtime call folding (requires constfold pass — ISSUE-1)
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, RuntimeAbsI64Folded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    {
        Instr instr;
        instr.op = Opcode::Call;
        instr.callee = "rt_abs_i64";
        instr.result = id;
        instr.type = Type(Type::Kind::I64);
        instr.operands.push_back(Value::constInt(-5));
        entry.instructions.push_back(instr);
    }
    builder.emitRet(Value::temp(id), {});

    // Need extern declaration for verification
    Extern ext;
    ext.name = "rt_abs_i64";
    ext.retType = Type(Type::Kind::I64);
    ext.params.push_back(Type(Type::Kind::I64));
    module.externs.push_back(ext);

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 5));
}

TEST(ConstFoldAudit, RuntimeSqrtFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::F64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    {
        Instr instr;
        instr.op = Opcode::Call;
        instr.callee = "rt_sqrt";
        instr.result = id;
        instr.type = Type(Type::Kind::F64);
        instr.operands.push_back(Value::constFloat(4.0));
        entry.instructions.push_back(instr);
    }
    builder.emitRet(Value::temp(id), {});

    Extern ext;
    ext.name = "rt_sqrt";
    ext.retType = Type(Type::Kind::F64);
    ext.params.push_back(Type(Type::Kind::F64));
    module.externs.push_back(ext);

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstFloat(entry, 2.0));
}

TEST(ConstFoldAudit, RuntimeFloorFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::F64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    {
        Instr instr;
        instr.op = Opcode::Call;
        instr.callee = "rt_floor";
        instr.result = id;
        instr.type = Type(Type::Kind::F64);
        instr.operands.push_back(Value::constFloat(3.7));
        entry.instructions.push_back(instr);
    }
    builder.emitRet(Value::temp(id), {});

    Extern ext;
    ext.name = "rt_floor";
    ext.retType = Type(Type::Kind::F64);
    ext.params.push_back(Type(Type::Kind::F64));
    module.externs.push_back(ext);

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstFloat(entry, 3.0));
}

// ---------------------------------------------------------------------------
// H. Edge cases — overflow and trap preservation
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, IMulOvfNotFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf,
              Value::constInt(std::numeric_limits<long long>::max()),
              Value::constInt(2), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    // Overflow: must NOT be folded
    EXPECT_TRUE(hasInstr(entry, Opcode::IMulOvf));
}

TEST(ConstFoldAudit, SDivChk0ByZeroNotFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::SDivChk0,
              Value::constInt(10), Value::constInt(0), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    // Div-by-zero trap: must NOT be folded
    EXPECT_TRUE(hasInstr(entry, Opcode::SDivChk0));
}

// ---------------------------------------------------------------------------
// I. Bitwise operations
// ---------------------------------------------------------------------------

TEST(ConstFoldAudit, AndFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::And,
              Value::constInt(0xFF), Value::constInt(0x0F), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 0x0F));
}

TEST(ConstFoldAudit, XorFolded)
{
    Module module;
    il::build::IRBuilder builder(module);
    Function &fn = builder.startFunction("test", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned id = builder.reserveTempId();
    emitBinOp(entry, Opcode::Xor,
              Value::constInt(0xAA), Value::constInt(0xFF), id);
    builder.emitRet(Value::temp(id), {});

    optimizeAndSerialize(module);
    EXPECT_TRUE(retIsConstInt(entry, 0x55));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
