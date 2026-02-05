//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/peephole_use_count.cpp
// Purpose: Verify peephole pass correctness and performance with precomputed
//          use counts, especially for large functions.
// Key invariants:
//   - Algebraic identities are applied correctly (add 0, mul 1, etc.)
//   - CBr simplification with single-use predicates removes dead comparisons
//   - Large functions do not exhibit O(N^2) compile-time behavior
// Ownership/Lifetime: Constructs local modules and runs the peephole pass.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/transform/Peephole.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include "tests/TestHarness.hpp"
#include <chrono>
#include <iostream>
#include <optional>

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

/// @brief Helper to build a binary instruction manually.
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

} // namespace

TEST(IL, testAddZeroIdentity)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_add_zero", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned temp0 = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(10), Value::constInt(2), temp0);

    unsigned resultId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::temp(temp0), Value::constInt(0), resultId);
    builder.emitRet(Value::temp(resultId), {});

    verifyOrDie(module);

    il::transform::peephole(module);

    ASSERT_EQ(entry.instructions.size(), 2);
    const Instr &ret = entry.instructions[1];
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_EQ(ret.operands.size(), 1);
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == temp0);
}

TEST(IL, testMulOneIdentity)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_mul_one", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned tempId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(5), Value::constInt(2), tempId);

    unsigned resultId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(1), Value::temp(tempId), resultId);
    builder.emitRet(Value::temp(resultId), {});

    verifyOrDie(module);

    il::transform::peephole(module);

    const Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == tempId);
}

TEST(IL, testShiftZeroIdentity)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_shl_zero", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned tempId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(7), Value::constInt(2), tempId);

    unsigned resultId = builder.reserveTempId();
    emitBinOp(entry, Opcode::Shl, Value::temp(tempId), Value::constInt(0), resultId);
    builder.emitRet(Value::temp(resultId), {});

    verifyOrDie(module);

    il::transform::peephole(module);

    const Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == tempId);
}

TEST(IL, testPlainAddZeroIdentity)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_plain_add_zero", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned base = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(3), Value::constInt(4), base);

    unsigned addId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::temp(base), Value::constInt(0), addId);
    builder.emitRet(Value::temp(addId), {});

    verifyOrDie(module);

    il::transform::peephole(module);

    const Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == base);
}

TEST(IL, testPlainMulOneIdentity)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_plain_mul_one", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned base = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::constInt(8), Value::constInt(2), base);

    unsigned mulId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(1), Value::temp(base), mulId);
    builder.emitRet(Value::temp(mulId), {});

    verifyOrDie(module);

    il::transform::peephole(module);

    const Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == base);
}

TEST(IL, testNoFoldIsubZeroLHS)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_isub_no_fold", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned x = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::constInt(1), Value::constInt(2), x);

    unsigned subId = builder.reserveTempId();
    emitBinOp(entry, Opcode::ISubOvf, Value::constInt(0), Value::temp(x), subId);
    builder.emitRet(Value::temp(subId), {});

    verifyOrDie(module);

    il::transform::peephole(module);

    ASSERT_EQ(entry.instructions.size(), 3);
    const Instr &ret = entry.instructions.back();
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == subId);
}

TEST(IL, testMulZeroAnnihilation)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_mul_zero", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned base = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::constInt(2), Value::constInt(3), base);

    unsigned mulId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::temp(base), Value::constInt(0), mulId);
    builder.emitRet(Value::temp(mulId), {});

    verifyOrDie(module);
    il::transform::peephole(module);
    verifyOrDie(module);

    ASSERT_EQ(entry.instructions.size(), 2);
    const Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.operands[0].kind, Value::Kind::ConstInt);
    ASSERT_EQ(ret.operands[0].i64, 0);
}

TEST(IL, testAndZeroAnnihilation)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_and_zero", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned base = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::constInt(4), Value::constInt(6), base);

    unsigned andId = builder.reserveTempId();
    emitBinOp(entry, Opcode::And, Value::temp(base), Value::constInt(0), andId);
    builder.emitRet(Value::temp(andId), {});

    verifyOrDie(module);
    il::transform::peephole(module);
    verifyOrDie(module);

    ASSERT_EQ(entry.instructions.size(), 2);
    const Instr &ret = entry.instructions.back();
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::ConstInt && ret.operands[0].i64 == 0);
}

TEST(IL, testXorSameOperand)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_xor_same", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned x = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(7), Value::constInt(3), x);

    unsigned xorId = builder.reserveTempId();
    emitBinOp(entry, Opcode::Xor, Value::temp(x), Value::temp(x), xorId);
    builder.emitRet(Value::temp(xorId), {});

    verifyOrDie(module);
    il::transform::peephole(module);
    verifyOrDie(module);

    ASSERT_EQ(entry.instructions.size(), 2);
    const Instr &ret = entry.instructions.back();
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::ConstInt && ret.operands[0].i64 == 0);
}

TEST(IL, testCmpReflexive)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_cmp_reflexive", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned x = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(5), Value::constInt(5), x);

    unsigned cmpId = builder.reserveTempId();
    emitBinOp(entry, Opcode::ICmpEq, Value::temp(x), Value::temp(x), cmpId, Type(Type::Kind::I1));
    builder.emitRet(Value::temp(cmpId), {});

    verifyOrDie(module);
    il::transform::peephole(module);
    verifyOrDie(module);

    ASSERT_EQ(entry.instructions.size(), 2);
    const Instr &ret = entry.instructions.back();
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::ConstInt && ret.operands[0].isBool);
    ASSERT_EQ(ret.operands[0].i64, 1);
}

TEST(IL, testNoFoldIMulMinusOne)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_mul_minus_one", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    unsigned x = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::constInt(10), Value::constInt(2), x);

    unsigned mulId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::temp(x), Value::constInt(-1), mulId);
    builder.emitRet(Value::temp(mulId), {});

    verifyOrDie(module);
    il::transform::peephole(module);
    verifyOrDie(module);

    ASSERT_EQ(entry.instructions.size(), 3);
    const Instr &ret = entry.instructions.back();
    ASSERT_TRUE(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == mulId);
}

TEST(IL, testCBrConstantFold)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_cbr_fold", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "then");
    builder.createBlock(fn, "else");

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &thenBlock = fn.blocks[1];
    BasicBlock &elseBlock = fn.blocks[2];

    builder.setInsertPoint(entry);
    unsigned cmpId = builder.reserveTempId();
    emitBinOp(
        entry, Opcode::ICmpEq, Value::constInt(5), Value::constInt(5), cmpId, Type(Type::Kind::I1));
    builder.cbr(Value::temp(cmpId), thenBlock, {}, elseBlock, {});

    builder.setInsertPoint(thenBlock);
    builder.emitRet(Value::constInt(1), {});

    builder.setInsertPoint(elseBlock);
    builder.emitRet(Value::constInt(0), {});

    verifyOrDie(module);

    il::transform::peephole(module);

    ASSERT_EQ(entry.instructions.size(), 1);
    const Instr &br = entry.instructions[0];
    ASSERT_EQ(br.op, Opcode::Br);
    ASSERT_TRUE(br.labels.size() == 1 && br.labels[0] == "then");
}

TEST(IL, testCBrSameTargetFold)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_cbr_same", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "target");

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &target = fn.blocks[1];

    builder.setInsertPoint(entry);
    unsigned condId = builder.reserveTempId();
    emitBinOp(entry,
              Opcode::ICmpEq,
              Value::constInt(1),
              Value::constInt(2),
              condId,
              Type(Type::Kind::I1));
    builder.cbr(Value::temp(condId), target, {}, target, {});

    builder.setInsertPoint(target);
    builder.emitRet(std::nullopt, {});

    verifyOrDie(module);

    il::transform::peephole(module);

    const Instr &br = entry.instructions.back();
    ASSERT_EQ(br.op, Opcode::Br);
    ASSERT_TRUE(br.labels.size() == 1 && br.labels[0] == "target");
    ASSERT_TRUE(br.operands.empty());
}

TEST(IL, testLargeFunctionPerformance)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("large_fn", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    constexpr size_t numOps = 1000;
    unsigned prevId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(3), Value::constInt(7), prevId);

    for (size_t i = 0; i < numOps; ++i)
    {
        unsigned addId = builder.reserveTempId();
        if (i % 2 == 0)
        {
            emitBinOp(entry, Opcode::IAddOvf, Value::temp(prevId), Value::constInt(0), addId);
        }
        else
        {
            emitBinOp(entry, Opcode::IAddOvf, Value::temp(prevId), Value::constInt(1), addId);
        }
        prevId = addId;
    }

    builder.emitRet(Value::temp(prevId), {});

    verifyOrDie(module);

    size_t instrsBefore = entry.instructions.size();

    auto start = std::chrono::high_resolution_clock::now();
    il::transform::peephole(module);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_TRUE(duration.count() < 5000);

    size_t instrsAfter = entry.instructions.size();
    ASSERT_TRUE(instrsAfter < instrsBefore);

    verifyOrDie(module);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
