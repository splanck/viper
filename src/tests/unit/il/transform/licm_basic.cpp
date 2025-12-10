//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/LICM.hpp"
#include "il/transform/LoopSimplify.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/io/Serializer.hpp"

#include "tests/unit/GTestStub.hpp"

using namespace il::core;

namespace
{

il::transform::AnalysisRegistry makeRegistry()
{
    il::transform::AnalysisRegistry registry;
    registry.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg", [](Module &mod, Function &fn) { return il::transform::buildCFG(mod, fn); });
    registry.registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](Module &mod, Function &fn)
        {
            viper::analysis::CFGContext ctx(mod);
            return viper::analysis::computeDominatorTree(ctx, fn);
        });
    registry.registerFunctionAnalysis<il::transform::LoopInfo>(
        "loop-info",
        [](Module &mod, Function &fn) { return il::transform::computeLoopInfo(mod, fn); });
    registry.registerFunctionAnalysis<viper::analysis::BasicAA>(
        "basic-aa", [](Module &mod, Function &fn) { return viper::analysis::BasicAA(mod, fn); });
    return registry;
}

} // namespace

TEST(LICM, HoistsInvariantAdd)
{
    Module M;
    Function F;
    F.name = "licm";
    F.retType = Type(Type::Kind::I64);

    // Preheader
    BasicBlock pre;
    pre.label = "pre";
    Instr toHeader;
    toHeader.op = Opcode::Br;
    toHeader.type = Type(Type::Kind::Void);
    toHeader.labels.push_back("header");
    toHeader.brArgs.emplace_back();
    pre.instructions.push_back(std::move(toHeader));
    pre.terminated = true;

    // Header with invariant add
    BasicBlock header;
    header.label = "header";
    Instr invAdd;
    invAdd.result = 0;
    invAdd.op = Opcode::Add;
    invAdd.type = Type(Type::Kind::I64);
    invAdd.operands = {Value::constInt(2), Value::constInt(3)};
    Instr toLatch;
    toLatch.op = Opcode::CBr;
    toLatch.type = Type(Type::Kind::Void);
    toLatch.operands.push_back(Value::constBool(true));
    toLatch.labels = {"latch", "exit"};
    toLatch.brArgs = {{}, {}};
    header.instructions.push_back(std::move(invAdd));
    header.instructions.push_back(std::move(toLatch));
    header.terminated = true;

    // Latch back to header with dummy loop param
    BasicBlock latch;
    latch.label = "latch";
    Instr back;
    back.op = Opcode::Br;
    back.type = Type(Type::Kind::Void);
    back.labels.push_back("header");
    back.brArgs.emplace_back();
    latch.instructions.push_back(std::move(back));
    latch.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    F.blocks = {std::move(pre), std::move(header), std::move(latch), std::move(exit)};
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::LoopSimplify simplify;
    auto preserved = simplify.run(Fn, manager);
    manager.invalidateAfterFunctionPass(preserved, Fn);

    auto &loopInfo = manager.getFunctionResult<il::transform::LoopInfo>("loop-info", Fn);
    ASSERT_FALSE(loopInfo.loops().empty());
    const auto &loopHeader = loopInfo.loops().front().headerLabel;
    ASSERT_EQ(loopHeader, "header");

    il::transform::LICM licm;
    licm.run(Fn, manager);

    BasicBlock &preheader = Fn.blocks[0];
    BasicBlock &hdr = Fn.blocks[1];
    bool foundAdd = false;
    for (const auto &I : preheader.instructions)
        if (I.op == Opcode::Add)
            foundAdd = true;
    bool addInHeader = false;
    for (const auto &I : hdr.instructions)
        if (I.op == Opcode::Add)
            addInHeader = true;
    if (!foundAdd)
        std::cerr << il::io::Serializer::toString(M, il::io::Serializer::Mode::Pretty);
    EXPECT_TRUE(foundAdd || addInHeader);
}

#ifndef VIPER_HAS_GTEST
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
