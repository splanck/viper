//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_merge_single_pred.cpp
// Purpose: Validate SimplifyCFG merges single-predecessor blocks into their parent.
// Key invariants: Instructions from the merged block relocate to the predecessor and the block is
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"

#include "tests/TestHarness.hpp"
#include <optional>
#include <string>
#include <vector>

TEST(IL, SimplifyCFGMergeSinglePred)
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction(
        "merge_single_pred", Type(Type::Kind::I64), {Param{"x", Type(Type::Kind::I64), 0}});

    builder.createBlock(fn, "entry");
    const std::vector<Param> midParams{Param{"v", Type(Type::Kind::I64), 0}};
    builder.createBlock(fn, "mid", midParams);
    const std::vector<Param> exitParams{Param{"result", Type(Type::Kind::I64), 0}};
    builder.createBlock(fn, "exit", exitParams);

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &mid = fn.blocks[1];
    BasicBlock &exit = fn.blocks[2];

    ASSERT_EQ(mid.params.size(), 1);
    ASSERT_EQ(exit.params.size(), 1);

    builder.setInsertPoint(entry);
    const Value paramX = Value::temp(fn.params[0].id);
    builder.br(mid, {paramX});

    builder.setInsertPoint(mid);
    const unsigned addId = builder.reserveTempId();
    Instr addInstr;
    addInstr.result = addId;
    addInstr.op = Opcode::IAddOvf;
    addInstr.type = Type(Type::Kind::I64);
    addInstr.operands.push_back(builder.blockParam(mid, 0));
    addInstr.operands.push_back(Value::constInt(5));
    mid.instructions.push_back(addInstr);

    const unsigned mulId = builder.reserveTempId();
    Instr mulInstr;
    mulInstr.result = mulId;
    mulInstr.op = Opcode::IMulOvf;
    mulInstr.type = Type(Type::Kind::I64);
    mulInstr.operands.push_back(Value::temp(addId));
    mulInstr.operands.push_back(Value::constInt(2));
    mid.instructions.push_back(mulInstr);

    builder.br(exit, {Value::temp(mulId)});

    builder.setInsertPoint(exit);
    builder.emitRet(std::optional<Value>{builder.blockParam(exit, 0)}, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verifyResult && "Module should verify before SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    ASSERT_TRUE(changed && "SimplifyCFG should merge the single-predecessor block");
    ASSERT_TRUE(stats.blocksMerged >= 1);

    const auto findBlock = [](const Function &function,
                              const std::string &label) -> const BasicBlock *
    {
        for (const auto &block : function.blocks)
        {
            if (block.label == label)
                return &block;
        }
        return nullptr;
    };

    const BasicBlock *entryBlock = findBlock(fn, "entry");
    ASSERT_TRUE(entryBlock && "Entry block must remain after merging");
    const BasicBlock *exitBlock = findBlock(fn, "exit");
    const BasicBlock *midBlock = findBlock(fn, "mid");
    ASSERT_FALSE(midBlock);

    ASSERT_TRUE(entryBlock->instructions.size() >= 3);
    const Instr &firstInstr = entryBlock->instructions[0];
    ASSERT_EQ(firstInstr.op, Opcode::IAddOvf);
    ASSERT_TRUE(firstInstr.result.has_value());
    ASSERT_EQ(firstInstr.operands.size(), 2);
    const Value &firstLhs = firstInstr.operands[0];
    ASSERT_EQ(firstLhs.kind, Value::Kind::Temp);
    ASSERT_EQ(firstLhs.id, fn.params[0].id);
    const Value &firstRhs = firstInstr.operands[1];
    ASSERT_TRUE(firstRhs.kind == Value::Kind::ConstInt && firstRhs.i64 == 5);

    const Instr &secondInstr = entryBlock->instructions[1];
    ASSERT_EQ(secondInstr.op, Opcode::IMulOvf);
    ASSERT_TRUE(secondInstr.result.has_value());
    ASSERT_EQ(secondInstr.operands.size(), 2);
    const Value &mulLhs = secondInstr.operands[0];
    ASSERT_EQ(mulLhs.kind, Value::Kind::Temp);
    ASSERT_EQ(mulLhs.id, *firstInstr.result);
    const Value &mulRhs = secondInstr.operands[1];
    ASSERT_TRUE(mulRhs.kind == Value::Kind::ConstInt && mulRhs.i64 == 2);

    const Instr &entryTerm = entryBlock->instructions.back();
    if (exitBlock)
    {
        ASSERT_EQ(entryTerm.op, Opcode::Br);
        ASSERT_TRUE(entryTerm.labels.size() == 1 && entryTerm.labels.front() == exitBlock->label);
        ASSERT_EQ(entryTerm.brArgs.size(), 1);
        ASSERT_EQ(entryTerm.brArgs.front().size(), 1);
        const Value &branchArg = entryTerm.brArgs.front().front();
        ASSERT_EQ(branchArg.kind, Value::Kind::Temp);
        ASSERT_EQ(branchArg.id, *secondInstr.result);
        ASSERT_EQ(exitBlock->params.size(), 1);
    }
    else
    {
        ASSERT_EQ(entryTerm.op, Opcode::Ret);
        ASSERT_EQ(entryTerm.operands.size(), 1);
        const Value &retValue = entryTerm.operands.front();
        ASSERT_EQ(retValue.kind, Value::Kind::Temp);
        ASSERT_EQ(retValue.id, *secondInstr.result);
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
