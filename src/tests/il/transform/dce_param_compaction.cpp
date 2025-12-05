//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/dce_param_compaction.cpp
// Purpose: Verify DCE block parameter pruning with compaction strategy.
// Key invariants:
//   - Unused block parameters are removed.
//   - Corresponding branch arguments are removed in sync.
//   - Multiple predecessors are handled correctly.
//   - Many parameters with selective removal work correctly.
// Ownership/Lifetime: Constructs local modules and runs DCE pass.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/transform/DCE.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>
#include <chrono>
#include <optional>

using namespace il::core;

namespace
{

/// @brief Test that a single unused block parameter is removed.
void testSingleUnusedParam()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_single", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "target", {Param{"unused", Type(Type::Kind::I64), 0}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &target = fn.blocks[1];

    builder.setInsertPoint(entry);
    builder.br(target, {Value::constInt(42)});

    builder.setInsertPoint(target);
    // Do NOT use the block param - return a constant instead
    builder.emitRet(Value::constInt(0), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before DCE");

    il::transform::dce(module);

    // The unused param should be removed
    assert(target.params.empty() && "Unused param should be removed");
    // The brArgs should also be empty
    const Instr &br = entry.instructions.back();
    assert(br.op == Opcode::Br);
    assert(br.brArgs.empty() || br.brArgs[0].empty() && "Branch args should be removed");

    verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify after DCE");
}

/// @brief Test that used block parameters are preserved.
void testUsedParamPreserved()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_used", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "target", {Param{"used", Type(Type::Kind::I64), 0}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &target = fn.blocks[1];

    builder.setInsertPoint(entry);
    builder.br(target, {Value::constInt(42)});

    builder.setInsertPoint(target);
    // Use the block param
    builder.emitRet(Value::temp(target.params[0].id), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before DCE");

    il::transform::dce(module);

    // The used param should be preserved
    assert(target.params.size() == 1 && "Used param should be preserved");
    const Instr &br = entry.instructions.back();
    assert(br.brArgs.size() == 1 && br.brArgs[0].size() == 1 && "Branch args should be preserved");
}

/// @brief Test selective removal: some params used, some not.
void testSelectiveRemoval()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_selective", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    // Four params: keep indices 0 and 2, remove 1 and 3
    builder.createBlock(fn, "target", {Param{"keep0", Type(Type::Kind::I64), 0},
                                       Param{"remove1", Type(Type::Kind::I64), 1},
                                       Param{"keep2", Type(Type::Kind::I64), 2},
                                       Param{"remove3", Type(Type::Kind::I64), 3}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &target = fn.blocks[1];

    builder.setInsertPoint(entry);
    builder.br(target, {Value::constInt(10), Value::constInt(20), Value::constInt(30), Value::constInt(40)});

    builder.setInsertPoint(target);
    // Use params 0 and 2, not 1 and 3
    unsigned sumId = builder.reserveTempId();
    Instr sum;
    sum.op = Opcode::IAddOvf;
    sum.type = Type(Type::Kind::I64);
    sum.result = sumId;
    sum.operands.push_back(Value::temp(target.params[0].id)); // keep0
    sum.operands.push_back(Value::temp(target.params[2].id)); // keep2
    target.instructions.push_back(sum);
    builder.emitRet(Value::temp(sumId), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before DCE");

    il::transform::dce(module);

    // Should have 2 params remaining
    assert(target.params.size() == 2 && "Should keep 2 params");
    // brArgs should have 2 values (10 and 30)
    const Instr &br = entry.instructions.back();
    assert(br.brArgs.size() == 1 && br.brArgs[0].size() == 2 && "Should have 2 branch args");
    assert(br.brArgs[0][0].kind == Value::Kind::ConstInt && br.brArgs[0][0].i64 == 10);
    assert(br.brArgs[0][1].kind == Value::Kind::ConstInt && br.brArgs[0][1].i64 == 30);

    verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify after DCE");
}

/// @brief Test with multiple predecessors.
void testMultiplePredecessors()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_multi_pred", Type(Type::Kind::I64),
                                         {Param{"flag", Type(Type::Kind::I1), 0}});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "left");
    builder.createBlock(fn, "right");
    // Two params: first is used, second is not
    builder.createBlock(fn, "join", {Param{"used", Type(Type::Kind::I64), 1},
                                     Param{"unused", Type(Type::Kind::I64), 2}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &left = fn.blocks[1];
    BasicBlock &right = fn.blocks[2];
    BasicBlock &join = fn.blocks[3];

    builder.setInsertPoint(entry);
    builder.cbr(Value::temp(fn.params[0].id), left, {}, right, {});

    builder.setInsertPoint(left);
    builder.br(join, {Value::constInt(100), Value::constInt(1)});

    builder.setInsertPoint(right);
    builder.br(join, {Value::constInt(200), Value::constInt(2)});

    builder.setInsertPoint(join);
    // Only use the first param
    builder.emitRet(Value::temp(join.params[0].id), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before DCE");

    il::transform::dce(module);

    // Should have 1 param remaining
    assert(join.params.size() == 1 && "Should keep 1 param");

    // Both predecessors should have their brArgs updated
    const Instr &leftBr = left.instructions.back();
    assert(leftBr.brArgs.size() == 1 && leftBr.brArgs[0].size() == 1);
    assert(leftBr.brArgs[0][0].i64 == 100);

    const Instr &rightBr = right.instructions.back();
    assert(rightBr.brArgs.size() == 1 && rightBr.brArgs[0].size() == 1);
    assert(rightBr.brArgs[0][0].i64 == 200);

    verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify after DCE");
}

/// @brief Test with CBr having both edges to the same block.
void testCBrSameTarget()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_cbr_same", Type(Type::Kind::I64),
                                         {Param{"flag", Type(Type::Kind::I1), 0}});
    builder.createBlock(fn, "entry");
    // Two params: first is used, second is not
    builder.createBlock(fn, "target", {Param{"used", Type(Type::Kind::I64), 1},
                                       Param{"unused", Type(Type::Kind::I64), 2}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &target = fn.blocks[1];

    builder.setInsertPoint(entry);
    // CBr with same target for both branches, different args
    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.operands.push_back(Value::temp(fn.params[0].id));
    cbr.labels = {"target", "target"};
    cbr.brArgs = {{Value::constInt(10), Value::constInt(1)},
                  {Value::constInt(20), Value::constInt(2)}};
    entry.instructions.push_back(cbr);

    builder.setInsertPoint(target);
    builder.emitRet(Value::temp(target.params[0].id), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before DCE");

    il::transform::dce(module);

    // Should have 1 param remaining
    assert(target.params.size() == 1 && "Should keep 1 param");

    // Both brArgs sets should be updated
    const Instr &cbrInstr = entry.instructions.back();
    assert(cbrInstr.brArgs.size() == 2);
    assert(cbrInstr.brArgs[0].size() == 1 && cbrInstr.brArgs[0][0].i64 == 10);
    assert(cbrInstr.brArgs[1].size() == 1 && cbrInstr.brArgs[1][0].i64 == 20);

    verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify after DCE");
}

/// @brief Test performance with many parameters and multiple predecessors.
void testManyParamsAndPreds()
{
    Module module;
    il::build::IRBuilder builder(module);

    // Use a simpler CFG: entry -> left/right -> target
    // But with many parameters
    constexpr size_t numParams = 100;

    Function &fn = builder.startFunction("test_many", Type(Type::Kind::I64),
                                         {Param{"flag", Type(Type::Kind::I1), 0}});

    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "left");
    builder.createBlock(fn, "right");

    // Create target block with many params (alternating used/unused)
    std::vector<Param> params;
    for (size_t i = 0; i < numParams; ++i)
    {
        params.push_back(Param{"p" + std::to_string(i), Type(Type::Kind::I64), static_cast<unsigned>(i + 1)});
    }
    builder.createBlock(fn, "target", params);

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &left = fn.blocks[1];
    BasicBlock &right = fn.blocks[2];
    BasicBlock &target = fn.blocks[3];

    // Entry branches to left or right
    builder.setInsertPoint(entry);
    builder.cbr(Value::temp(fn.params[0].id), left, {}, right, {});

    // Left and right both branch to target with args
    builder.setInsertPoint(left);
    {
        std::vector<Value> args;
        for (size_t j = 0; j < numParams; ++j)
            args.push_back(Value::constInt(static_cast<long long>(j)));
        builder.br(target, args);
    }

    builder.setInsertPoint(right);
    {
        std::vector<Value> args;
        for (size_t j = 0; j < numParams; ++j)
            args.push_back(Value::constInt(static_cast<long long>(j + 1000)));
        builder.br(target, args);
    }

    // In target, only use even-indexed params
    builder.setInsertPoint(target);
    unsigned accId = builder.reserveTempId();
    // Initialize accumulator with first even param + 0
    {
        Instr init;
        init.op = Opcode::IAddOvf;
        init.type = Type(Type::Kind::I64);
        init.result = accId;
        init.operands.push_back(Value::temp(target.params[0].id));
        init.operands.push_back(Value::constInt(0));
        target.instructions.push_back(init);
    }
    for (size_t i = 2; i < numParams; i += 2)
    {
        unsigned newId = builder.reserveTempId();
        Instr add;
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.result = newId;
        add.operands.push_back(Value::temp(accId));
        add.operands.push_back(Value::temp(target.params[i].id));
        target.instructions.push_back(add);
        accId = newId;
    }
    builder.emitRet(Value::temp(accId), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before DCE");

    // Time the DCE pass
    auto start = std::chrono::high_resolution_clock::now();
    il::transform::dce(module);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // With compaction, this should be fast (under 1 second)
    assert(duration.count() < 5000 && "DCE should complete quickly");

    // Should have 50 params remaining (even indices)
    assert(target.params.size() == numParams / 2 && "Should keep half the params");

    // Both predecessors should have their brArgs updated
    const Instr &leftBr = left.instructions.back();
    assert(leftBr.brArgs.size() == 1 && leftBr.brArgs[0].size() == numParams / 2);

    const Instr &rightBr = right.instructions.back();
    assert(rightBr.brArgs.size() == 1 && rightBr.brArgs[0].size() == numParams / 2);

    verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify after DCE");
}

/// @brief Test that all params being unused results in empty param list.
void testAllParamsUnused()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_all_unused", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "target", {Param{"a", Type(Type::Kind::I64), 0},
                                       Param{"b", Type(Type::Kind::I64), 1},
                                       Param{"c", Type(Type::Kind::I64), 2}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &target = fn.blocks[1];

    builder.setInsertPoint(entry);
    builder.br(target, {Value::constInt(1), Value::constInt(2), Value::constInt(3)});

    builder.setInsertPoint(target);
    // Don't use any params
    builder.emitRet(Value::constInt(0), {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before DCE");

    il::transform::dce(module);

    assert(target.params.empty() && "All params should be removed");
    const Instr &br = entry.instructions.back();
    assert(br.brArgs.empty() || br.brArgs[0].empty() && "All branch args should be removed");

    verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify after DCE");
}

} // namespace

int main()
{
    testSingleUnusedParam();
    testUsedParamPreserved();
    testSelectiveRemoval();
    testMultiplePredecessors();
    testCBrSameTarget();
    testManyParamsAndPreds();
    testAllParamsUnused();

    return 0;
}
