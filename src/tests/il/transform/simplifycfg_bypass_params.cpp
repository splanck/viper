//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_bypass_params.cpp
// Purpose: Verify SimplifyCFG forwards branch arguments when bypassing blocks with params.
// Key invariants: Forwarding block removal must preserve branch arguments and remove the block.
// Ownership/Lifetime: Constructs a local module and runs the pass by value.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"

#include "tests/TestHarness.hpp"
#include <optional>
#include <string>

TEST(IL, SimplifyCFGBypassParams)
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("bypass", Type(Type::Kind::I64), {});
    BasicBlock &entry = builder.createBlock(fn, "entry");
    BasicBlock &mid = builder.createBlock(fn, "mid", {Param{"p", Type(Type::Kind::I64), 0}});
    BasicBlock &exit = builder.createBlock(fn, "exit", {Param{"result", Type(Type::Kind::I64), 0}});

    builder.setInsertPoint(entry);
    builder.br(mid, {Value::constInt(7)});

    builder.setInsertPoint(mid);
    builder.br(exit, {builder.blockParam(mid, 0)});

    builder.setInsertPoint(exit);
    builder.emitRet(std::optional<Value>{builder.blockParam(exit, 0)}, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verifyResult && "Module should verify before SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    ASSERT_TRUE(changed && "SimplifyCFG should remove the forwarding block");
    ASSERT_EQ(stats.predsMerged, 1);
    ASSERT_EQ(stats.emptyBlocksRemoved, 1);

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
    ASSERT_TRUE(entryBlock);
    const BasicBlock *exitBlock = findBlock(fn, "exit");
    ASSERT_TRUE(exitBlock);
    const BasicBlock *midBlock = findBlock(fn, "mid");
    ASSERT_FALSE(midBlock);

    ASSERT_FALSE(entryBlock->instructions.empty());
    const Instr &entryTerm = entryBlock->instructions.back();
    ASSERT_EQ(entryTerm.op, Opcode::Br);
    ASSERT_TRUE(entryTerm.labels.size() == 1 && entryTerm.labels.front() == exitBlock->label);
    ASSERT_TRUE(entryTerm.brArgs.size() == 1 && entryTerm.brArgs.front().size() == 1);
    const Value &bypassedArg = entryTerm.brArgs.front().front();
    ASSERT_EQ(bypassedArg.kind, Value::Kind::ConstInt);
    ASSERT_EQ(bypassedArg.i64, 7);

    ASSERT_EQ(exitBlock->params.size(), 1);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
