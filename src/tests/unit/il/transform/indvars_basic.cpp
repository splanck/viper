//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/IndVarSimplify.hpp"
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

TEST(IndVarSimplify, StrengthReductionApplies)
{
    Module M;
    Function F;
    F.name = "indvars";
    F.retType = Type(Type::Kind::Void);

    // Preheader -> header
    BasicBlock pre;
    pre.label = "pre";
    Instr toHeader;
    toHeader.op = Opcode::Br;
    toHeader.type = Type(Type::Kind::Void);
    toHeader.labels.push_back("header");
    toHeader.brArgs.push_back({Value::constInt(0)});
    pre.instructions.push_back(std::move(toHeader));
    pre.terminated = true;

    // Header with induction param i
    BasicBlock header;
    header.label = "header";
    Param i{"i", Type(Type::Kind::I64), 0};
    header.params.push_back(i);
    Instr mul;
    mul.result = 1;
    mul.op = Opcode::Mul;
    mul.type = Type(Type::Kind::I64);
    mul.operands = {Value::temp(i.id), Value::constInt(8)};
    Instr addr;
    addr.result = 2;
    addr.op = Opcode::Add;
    addr.type = Type(Type::Kind::I64);
    addr.operands = {Value::constInt(100), Value::temp(*mul.result)};
    Instr toLatch;
    toLatch.op = Opcode::Br;
    toLatch.type = Type(Type::Kind::Void);
    toLatch.labels.push_back("latch");
    toLatch.brArgs.push_back({Value::temp(i.id)});
    header.instructions = {std::move(mul), std::move(addr), std::move(toLatch)};
    header.terminated = true;

    // Latch increments i and branches to exit
    BasicBlock latch;
    latch.label = "latch";
    Param li{"li", Type(Type::Kind::I64), 3};
    latch.params.push_back(li);
    Instr inc;
    inc.result = 4;
    inc.op = Opcode::Add;
    inc.type = Type(Type::Kind::I64);
    inc.operands = {Value::temp(li.id), Value::constInt(1)};
    Instr back;
    back.op = Opcode::CBr;
    back.type = Type(Type::Kind::Void);
    back.operands.push_back(Value::constBool(false));
    back.labels = {"header", "exit"};
    back.brArgs = {{Value::temp(*inc.result)}, {}};
    latch.instructions = {std::move(inc), std::move(back)};
    latch.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    F.blocks = {std::move(pre), std::move(header), std::move(latch), std::move(exit)};
    F.valueNames.resize(5);
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::LoopSimplify simplify;
    simplify.run(Fn, manager);

    il::transform::IndVarSimplify indvars;
    indvars.run(Fn, manager);

    // Header should gain a new carried address parameter
    BasicBlock &hdr = Fn.blocks[1];
    ASSERT_TRUE(hdr.params.size() >= 2U);
    // Original addr add should be replaced by param use
    bool hasAddrAdd = false;
    for (auto &I : hdr.instructions)
        if (I.op == Opcode::Add && I.result && *I.result == 2)
            hasAddrAdd = true;
    EXPECT_FALSE(hasAddrAdd);
}

TEST(IndVarSimplify, SkipsNonCanonicalLoop)
{
    Module M;
    Function F;
    F.name = "indvars_skip";
    F.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    Instr br;
    br.op = Opcode::Br;
    br.type = Type(Type::Kind::Void);
    br.labels.push_back("header");
    br.brArgs.emplace_back();
    entry.instructions.push_back(std::move(br));
    entry.terminated = true;

    BasicBlock header;
    header.label = "header";
    Instr toLatch;
    toLatch.op = Opcode::Br;
    toLatch.type = Type(Type::Kind::Void);
    toLatch.labels.push_back("latch1");
    toLatch.brArgs.emplace_back();
    header.instructions.push_back(std::move(toLatch));
    header.terminated = true;

    BasicBlock latch1;
    latch1.label = "latch1";
    Instr br1;
    br1.op = Opcode::Br;
    br1.type = Type(Type::Kind::Void);
    br1.labels.push_back("latch2");
    br1.brArgs.emplace_back();
    latch1.instructions.push_back(std::move(br1));
    latch1.terminated = true;

    BasicBlock latch2;
    latch2.label = "latch2";
    Instr back2;
    back2.op = Opcode::Br;
    back2.type = Type(Type::Kind::Void);
    back2.labels.push_back("header");
    back2.brArgs.emplace_back();
    latch2.instructions.push_back(std::move(back2));
    latch2.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    F.blocks = {
        std::move(entry), std::move(header), std::move(latch1), std::move(latch2), std::move(exit)};
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::LoopSimplify simplify;
    simplify.run(Fn, manager);

    il::transform::IndVarSimplify indvars;
    indvars.run(Fn, manager);

    // Loop should remain unchanged because of multiple latches
    BasicBlock &hdr = Fn.blocks[1];
    EXPECT_EQ(hdr.params.size(), 0U);
}

#ifndef VIPER_HAS_GTEST
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
