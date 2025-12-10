//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "il/transform/LoopSimplify.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "tests/unit/GTestStub.hpp"

using namespace il::core;
using il::transform::Loop;

TEST(LoopSimplify, LoopInfoCapturesLatchesAndExits)
{
    Module M;
    Function F;
    F.name = "loops";
    F.retType = Type(Type::Kind::Void);

    // entry -> outer header
    BasicBlock entry;
    entry.label = "entry";
    Instr brEntry;
    brEntry.op = Opcode::Br;
    brEntry.type = Type(Type::Kind::Void);
    brEntry.labels.push_back("outer");
    brEntry.brArgs.emplace_back();
    entry.instructions.push_back(std::move(brEntry));
    entry.terminated = true;

    // outer header branches into inner loop then exits to outer_exit
    BasicBlock outer;
    outer.label = "outer";
    Instr outerCbr;
    outerCbr.op = Opcode::CBr;
    outerCbr.type = Type(Type::Kind::Void);
    outerCbr.operands.push_back(Value::constBool(true));
    outerCbr.labels = {"inner", "outer_exit"};
    outerCbr.brArgs.resize(2);
    outer.instructions.push_back(std::move(outerCbr));
    outer.terminated = true;

    // inner header -> inner latch
    BasicBlock inner;
    inner.label = "inner";
    Instr innerBr;
    innerBr.op = Opcode::Br;
    innerBr.type = Type(Type::Kind::Void);
    innerBr.labels.push_back("inner_latch");
    innerBr.brArgs.emplace_back();
    inner.instructions.push_back(std::move(innerBr));
    inner.terminated = true;

    // inner latch back to inner header
    BasicBlock innerLatch;
    innerLatch.label = "inner_latch";
    Instr backInner;
    backInner.op = Opcode::Br;
    backInner.type = Type(Type::Kind::Void);
    backInner.labels.push_back("inner");
    backInner.brArgs.emplace_back();
    innerLatch.instructions.push_back(std::move(backInner));
    innerLatch.terminated = true;

    // after inner loop, continue to outer latch
    BasicBlock afterInner;
    afterInner.label = "after_inner";
    Instr toLatch;
    toLatch.op = Opcode::Br;
    toLatch.type = Type(Type::Kind::Void);
    toLatch.labels.push_back("outer_latch");
    toLatch.brArgs.emplace_back();
    afterInner.instructions.push_back(std::move(toLatch));
    afterInner.terminated = true;

    // connect inner exit to after_inner
    Instr &innerTerm = inner.instructions.back();
    innerTerm.labels.push_back("after_inner");
    innerTerm.brArgs.push_back({});

    // outer latch back to outer header
    BasicBlock outerLatch;
    outerLatch.label = "outer_latch";
    Instr backOuter;
    backOuter.op = Opcode::Br;
    backOuter.type = Type(Type::Kind::Void);
    backOuter.labels.push_back("outer");
    backOuter.brArgs.emplace_back();
    outerLatch.instructions.push_back(std::move(backOuter));
    outerLatch.terminated = true;

    // outer exit returns
    BasicBlock outerExit;
    outerExit.label = "outer_exit";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    outerExit.instructions.push_back(std::move(ret));
    outerExit.terminated = true;

    F.blocks = {std::move(entry),
                std::move(outer),
                std::move(inner),
                std::move(innerLatch),
                std::move(afterInner),
                std::move(outerLatch),
                std::move(outerExit)};
    M.functions.push_back(std::move(F));

    auto info = il::transform::computeLoopInfo(M, M.functions.front());
    ASSERT_EQ(info.loops().size(), 2U);

    const Loop *outerLoop = info.findLoop("outer");
    ASSERT_NE(outerLoop, nullptr);
    EXPECT_TRUE(outerLoop->contains("outer_latch"));
    EXPECT_FALSE(outerLoop->contains("outer_exit"));
    EXPECT_EQ(outerLoop->latchLabels.size(), 1U);
    ASSERT_FALSE(outerLoop->exits.empty());

    const Loop *innerLoop = info.findLoop("inner");
    ASSERT_NE(innerLoop, nullptr);
    EXPECT_EQ(innerLoop->latchLabels.size(), 1U);
    EXPECT_TRUE(innerLoop->contains("inner_latch"));
    EXPECT_EQ(innerLoop->exits.size(), 1U);
    EXPECT_EQ(innerLoop->exits.front().to, "after_inner");
    EXPECT_EQ(innerLoop->parentHeader, "outer");
}

#ifndef VIPER_HAS_GTEST
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
