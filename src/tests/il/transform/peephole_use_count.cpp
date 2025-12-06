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

#include <cassert>
#include <chrono>
#include <optional>

using namespace il::core;

namespace
{

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

/// @brief Test that add x, 0 is simplified to x where x is a temp.
void testAddZeroIdentity()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_add_zero", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // Create a temp %0 that cannot be simplified
    unsigned temp0 = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(10), Value::constInt(2), temp0);

    // %1 = iadd.ovf %0, 0  -> should simplify to %0
    unsigned resultId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IAddOvf, Value::temp(temp0), Value::constInt(0), resultId);
    builder.emitRet(Value::temp(resultId), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before peephole");

    il::transform::peephole(module);

    // After peephole, the iadd.ovf +0 should be eliminated
    // The ret should now reference %0 (the mul result)
    assert(entry.instructions.size() == 2 && "Two instructions should remain (mul + ret)");
    const Instr &ret = entry.instructions[1];
    assert(ret.op == Opcode::Ret);
    assert(ret.operands.size() == 1);
    assert(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == temp0);
}

/// @brief Test that mul x, 1 is simplified to x.
void testMulOneIdentity()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_mul_one", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // Create a temp %0 that won't be simplified (mul by 2, not 1)
    unsigned tempId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(5), Value::constInt(2), tempId);

    // %1 = imul.ovf 1, %0  -> should simplify to %0
    unsigned resultId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(1), Value::temp(tempId), resultId);
    builder.emitRet(Value::temp(resultId), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before peephole");

    il::transform::peephole(module);

    // Check that ret uses %0
    const Instr &ret = entry.instructions.back();
    assert(ret.op == Opcode::Ret);
    assert(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == tempId);
}

/// @brief Test that shift by 0 is simplified to the input.
void testShiftZeroIdentity()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_shl_zero", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // Create a temp %0 that won't be simplified (mul by 2)
    unsigned tempId = builder.reserveTempId();
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(7), Value::constInt(2), tempId);

    // %1 = shl %0, 0  -> should simplify to %0
    unsigned resultId = builder.reserveTempId();
    emitBinOp(entry, Opcode::Shl, Value::temp(tempId), Value::constInt(0), resultId);
    builder.emitRet(Value::temp(resultId), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before peephole");

    il::transform::peephole(module);

    const Instr &ret = entry.instructions.back();
    assert(ret.op == Opcode::Ret);
    assert(ret.operands[0].kind == Value::Kind::Temp && ret.operands[0].id == tempId);
}

/// @brief Test that CBr with constant condition folds to Br and removes
///        single-use comparison instruction.
void testCBrConstantFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_cbr_fold", Type(Type::Kind::I64), {});
    // Create all blocks first to avoid invalidating references
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "then");
    builder.createBlock(fn, "else");

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &thenBlock = fn.blocks[1];
    BasicBlock &elseBlock = fn.blocks[2];

    builder.setInsertPoint(entry);
    // %0 = icmp.eq 5, 5  -> always true
    unsigned cmpId = builder.reserveTempId();
    emitBinOp(
        entry, Opcode::ICmpEq, Value::constInt(5), Value::constInt(5), cmpId, Type(Type::Kind::I1));
    // cbr %0, ^then, ^else
    builder.cbr(Value::temp(cmpId), thenBlock, {}, elseBlock, {});

    builder.setInsertPoint(thenBlock);
    builder.emitRet(Value::constInt(1), {});

    builder.setInsertPoint(elseBlock);
    builder.emitRet(Value::constInt(0), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before peephole");

    il::transform::peephole(module);

    // The comparison should be removed (single use) and cbr folded to br
    assert(entry.instructions.size() == 1 && "Only br should remain in entry");
    const Instr &br = entry.instructions[0];
    assert(br.op == Opcode::Br && "Should be unconditional branch");
    assert(br.labels.size() == 1 && br.labels[0] == "then");
}

/// @brief Test that CBr with same-target branches becomes unconditional Br.
void testCBrSameTargetFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_cbr_same", Type(Type::Kind::Void), {});
    // Create all blocks first
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "target");

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &target = fn.blocks[1];

    builder.setInsertPoint(entry);
    // Create a temp condition
    unsigned condId = builder.reserveTempId();
    emitBinOp(entry,
              Opcode::ICmpEq,
              Value::constInt(1),
              Value::constInt(2),
              condId,
              Type(Type::Kind::I1));
    // cbr %cond, ^target, ^target  -> should become br ^target
    builder.cbr(Value::temp(condId), target, {}, target, {});

    builder.setInsertPoint(target);
    builder.emitRet(std::nullopt, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before peephole");

    il::transform::peephole(module);

    // The last instruction in entry should be br
    const Instr &br = entry.instructions.back();
    assert(br.op == Opcode::Br && "Should be unconditional branch");
    assert(br.labels.size() == 1 && br.labels[0] == "target");
    assert(br.operands.empty() && "Br should have no condition operand");
}

/// @brief Test performance with a moderately large single block.
/// @details Creates a large function with many instructions in a single block
///          to verify that the precomputed use-count map provides O(n) behavior.
void testLargeFunctionPerformance()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("large_fn", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // Create many instructions in a single block
    constexpr size_t numOps = 1000;
    unsigned prevId = builder.reserveTempId();
    // Start with a non-identity mul so it won't be folded
    emitBinOp(entry, Opcode::IMulOvf, Value::constInt(3), Value::constInt(7), prevId);

    for (size_t i = 0; i < numOps; ++i)
    {
        unsigned addId = builder.reserveTempId();
        // Some adds with 0 (will be folded), some with 1 (won't be)
        if (i % 2 == 0)
        {
            // add %prev, 0 -> should simplify to %prev
            emitBinOp(entry, Opcode::IAddOvf, Value::temp(prevId), Value::constInt(0), addId);
        }
        else
        {
            // add %prev, 1 -> won't simplify
            emitBinOp(entry, Opcode::IAddOvf, Value::temp(prevId), Value::constInt(1), addId);
        }
        prevId = addId;
    }

    builder.emitRet(Value::temp(prevId), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before peephole");

    size_t instrsBefore = entry.instructions.size();

    // Time the peephole pass
    auto start = std::chrono::high_resolution_clock::now();
    il::transform::peephole(module);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // With O(N) use counting, this should complete very quickly (under 1 second)
    assert(duration.count() < 5000 && "Peephole pass took too long");

    size_t instrsAfter = entry.instructions.size();
    // Should have removed ~500 add-0 instructions (every other one)
    assert(instrsAfter < instrsBefore && "Peephole should have removed some instructions");

    // Verify the module still verifies after transformation
    verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify after peephole");
}

} // namespace

int main()
{
    testAddZeroIdentity();
    testMulOneIdentity();
    testShiftZeroIdentity();
    testCBrConstantFold();
    testCBrSameTargetFold();
    testLargeFunctionPerformance();

    return 0;
}
