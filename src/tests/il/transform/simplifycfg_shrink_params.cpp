//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_shrink_params.cpp
// Purpose: Ensure SimplifyCFG drops block params that are identical across predecessors.
// Key invariants: Shared param is replaced by common value and removed from param list.
// Ownership/Lifetime: Constructs a local module and applies the pass in place.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"

#include "tests/TestHarness.hpp"
#include <optional>
#include <string>

TEST(IL, SimplifyCFGShrinkParams)
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction(
        "shrink_params", Type(Type::Kind::I64), {Param{"flag", Type(Type::Kind::I1), 0}});

    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "left");
    builder.createBlock(fn, "right");
    builder.createBlock(
        fn, "join", {Param{"a", Type(Type::Kind::I64), 0}, Param{"b", Type(Type::Kind::I64), 0}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &left = fn.blocks[1];
    BasicBlock &right = fn.blocks[2];
    BasicBlock &join = fn.blocks[3];

    builder.setInsertPoint(entry);
    const Value flag = Value::temp(fn.params[0].id);
    builder.cbr(flag, left, {}, right, {});

    builder.setInsertPoint(left);
    builder.br(join, {Value::constInt(99), Value::constInt(1)});

    builder.setInsertPoint(right);
    builder.br(join, {Value::constInt(99), Value::constInt(2)});

    builder.setInsertPoint(join);
    const unsigned sumId = builder.reserveTempId();
    Instr sum;
    sum.result = sumId;
    sum.op = Opcode::IAddOvf;
    sum.type = Type(Type::Kind::I64);
    sum.operands.push_back(builder.blockParam(join, 0));
    sum.operands.push_back(builder.blockParam(join, 1));
    join.instructions.push_back(sum);

    builder.emitRet(std::optional<Value>{Value::temp(sumId)}, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verifyResult && "Module should verify before SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    ASSERT_TRUE(changed && "SimplifyCFG should remove redundant block parameters");
    ASSERT_EQ(stats.paramsShrunk, 1);

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

    const BasicBlock *joinBlock = findBlock(fn, "join");
    ASSERT_TRUE(joinBlock);
    ASSERT_EQ(joinBlock->params.size(), 1);

    ASSERT_FALSE(joinBlock->instructions.empty());
    const Instr &sumInstr = joinBlock->instructions.front();
    ASSERT_EQ(sumInstr.operands.size(), 2);
    const Value &firstOperand = sumInstr.operands[0];
    ASSERT_EQ(firstOperand.kind, Value::Kind::ConstInt);
    ASSERT_EQ(firstOperand.i64, 99);
    const Value &secondOperand = sumInstr.operands[1];
    ASSERT_EQ(secondOperand.kind, Value::Kind::Temp);
    ASSERT_EQ(secondOperand.id, joinBlock->params[0].id);

    const BasicBlock *entryBlock = findBlock(fn, "entry");
    ASSERT_TRUE(entryBlock && !entryBlock->instructions.empty());
    const Instr &entryTerm = entryBlock->instructions.back();
    ASSERT_EQ(entryTerm.op, Opcode::CBr);
    ASSERT_EQ(entryTerm.labels.size(), 2);
    ASSERT_TRUE(entryTerm.labels[0] == "join" && entryTerm.labels[1] == "join");
    ASSERT_EQ(entryTerm.brArgs.size(), 2);
    ASSERT_EQ(entryTerm.brArgs[0].size(), 1);
    ASSERT_EQ(entryTerm.brArgs[1].size(), 1);
    const Value &trueArg = entryTerm.brArgs[0][0];
    const Value &falseArg = entryTerm.brArgs[1][0];
    ASSERT_TRUE(trueArg.kind == Value::Kind::ConstInt && trueArg.i64 == 1);
    ASSERT_TRUE(falseArg.kind == Value::Kind::ConstInt && falseArg.i64 == 2);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
