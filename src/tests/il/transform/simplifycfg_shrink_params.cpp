//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_shrink_params.cpp
// Purpose: Ensure SimplifyCFG drops block params that are identical across predecessors.
// Key invariants: Shared param is replaced by common value and removed from param list.
// Ownership/Lifetime: Constructs a local module and applies the pass in place.
// Links: docs/il/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"

#include "tests/TestHarness.hpp"
#include <optional>
#include <string>

TEST(IL, SimplifyCFGShrinkParams) {
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
                              const std::string &label) -> const BasicBlock * {
        for (const auto &block : function.blocks) {
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

TEST(IL, SimplifyCFGDoesNotShrinkParamsUsedInDominatedSuccessors) {
    using namespace il::core;

    Module module;
    Function fn;
    fn.name = "preserve_cross_block_param";
    fn.retType = Type(Type::Kind::I64);
    fn.params.push_back(Param{"flag", Type(Type::Kind::I1), 0});

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"carrier"};
        br.brArgs = {{Value::constInt(42)}};
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    BasicBlock carrier;
    carrier.label = "carrier";
    carrier.params.push_back(Param{"carried", Type(Type::Kind::I64), 1});
    {
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::temp(0)};
        cbr.labels = {"use", "latch"};
        cbr.brArgs = {{}, {}};
        carrier.instructions.push_back(std::move(cbr));
        carrier.terminated = true;
    }

    BasicBlock latch;
    latch.label = "latch";
    {
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::temp(0)};
        cbr.labels = {"carrier", "use"};
        cbr.brArgs = {{Value::constInt(42)}, {}};
        latch.instructions.push_back(std::move(cbr));
        latch.terminated = true;
    }

    BasicBlock use;
    use.label = "use";
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        use.instructions.push_back(std::move(ret));
        use.terminated = true;
    }

    fn.blocks = {std::move(entry), std::move(carrier), std::move(latch), std::move(use)};
    fn.valueNames.resize(2);
    module.functions.push_back(std::move(fn));

    ASSERT_TRUE(il::verify::Verifier::verify(module).hasValue());

    Function &function = module.functions.front();
    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    pass.run(function, &stats);

    auto verifyResult = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verifyResult && "SimplifyCFG must preserve dominated cross-block param uses");

    const BasicBlock *carrierBlock = nullptr;
    for (const auto &block : function.blocks)
        if (block.label == "carrier")
            carrierBlock = &block;
    ASSERT_NE(carrierBlock, nullptr);
    ASSERT_EQ(carrierBlock->params.size(), 1u);
    EXPECT_EQ(carrierBlock->params.front().id, 1u);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
