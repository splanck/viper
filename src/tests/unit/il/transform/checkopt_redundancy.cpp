//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/CheckOpt.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

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
    return registry;
}
} // namespace

TEST(CheckOpt, EliminatesRedundantInNestedLoops)
{
    Module M;
    Function F;
    F.name = "nested";
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

    // outer header with idxchk
    BasicBlock outer;
    outer.label = "outer";
    Instr idxOuter;
    idxOuter.result = 0;
    idxOuter.op = Opcode::IdxChk;
    idxOuter.type = Type(Type::Kind::I32);
    idxOuter.operands = {Value::constInt(5), Value::constInt(0), Value::constInt(10)};
    Instr toInner;
    toInner.op = Opcode::Br;
    toInner.type = Type(Type::Kind::Void);
    toInner.labels.push_back("inner");
    toInner.brArgs.emplace_back();
    outer.instructions = {std::move(idxOuter), std::move(toInner)};
    outer.terminated = true;

    // inner header repeats same idxchk
    BasicBlock inner;
    inner.label = "inner";
    Instr idxInner;
    idxInner.result = 1;
    idxInner.op = Opcode::IdxChk;
    idxInner.type = Type(Type::Kind::I32);
    idxInner.operands = {Value::constInt(5), Value::constInt(0), Value::constInt(10)};
    Instr back;
    back.op = Opcode::CBr;
    back.type = Type(Type::Kind::Void);
    back.operands.push_back(Value::constBool(false));
    back.labels = {"inner", "exit"};
    back.brArgs = {{}, {}};
    inner.instructions = {std::move(idxInner), std::move(back)};
    inner.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    F.blocks = {std::move(entry), std::move(outer), std::move(inner), std::move(exit)};
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::CheckOpt pass;
    pass.run(Fn, manager);

    // Inner idxchk should be removed as dominated by outer.
    BasicBlock &innerBlock = Fn.blocks[2];
    size_t chkCount = 0;
    for (const auto &I : innerBlock.instructions)
        if (I.op == Opcode::IdxChk)
            ++chkCount;
    EXPECT_EQ(chkCount, 0U);
}

TEST(CheckOpt, DoesNotEliminateAcrossSiblingBlocks)
{
    Module M;
    Function F;
    F.name = "siblings";
    F.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::constBool(true));
    cbr.labels = {"left", "right"};
    cbr.brArgs = {{}, {}};
    entry.instructions.push_back(std::move(cbr));
    entry.terminated = true;

    BasicBlock left;
    left.label = "left";
    Instr chkL;
    chkL.op = Opcode::SDivChk0;
    chkL.type = Type(Type::Kind::I64);
    chkL.operands = {Value::constInt(8), Value::constInt(2)};
    Instr brL;
    brL.op = Opcode::Br;
    brL.type = Type(Type::Kind::Void);
    brL.labels.push_back("merge");
    brL.brArgs.emplace_back();
    left.instructions = {std::move(chkL), std::move(brL)};
    left.terminated = true;

    BasicBlock right;
    right.label = "right";
    Instr chkR;
    chkR.op = Opcode::SDivChk0;
    chkR.type = Type(Type::Kind::I64);
    chkR.operands = {Value::constInt(8), Value::constInt(2)};
    Instr brR;
    brR.op = Opcode::Br;
    brR.type = Type(Type::Kind::Void);
    brR.labels.push_back("merge");
    brR.brArgs.emplace_back();
    right.instructions = {std::move(chkR), std::move(brR)};
    right.terminated = true;

    BasicBlock merge;
    merge.label = "merge";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    merge.instructions.push_back(std::move(ret));
    merge.terminated = true;

    F.blocks = {std::move(entry), std::move(left), std::move(right), std::move(merge)};
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::CheckOpt pass;
    pass.run(Fn, manager);

    // Both checks must remain because neither dominates the other.
    size_t checkCount = 0;
    for (const auto &B : Fn.blocks)
        for (const auto &I : B.instructions)
            if (I.op == Opcode::SDivChk0)
                ++checkCount;
    EXPECT_EQ(checkCount, 2U);
}

TEST(CheckOpt, PreservesTrapBehaviourWhenDominanceMissing)
{
    Module M;
    Function F;
    F.name = "trap_paths";
    F.retType = Type(Type::Kind::Void);

    // entry -> (if cond) then: check + trap branch; else: go to merge without check.
    BasicBlock entry;
    entry.label = "entry";
    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::constBool(true));
    cbr.labels = {"checked", "merge"};
    cbr.brArgs = {{}, {}};
    entry.instructions.push_back(std::move(cbr));
    entry.terminated = true;

    BasicBlock checked;
    checked.label = "checked";
    Instr chk;
    chk.op = Opcode::UDivChk0;
    chk.type = Type(Type::Kind::I64);
    chk.operands = {Value::constInt(1), Value::constInt(0)}; // would trap
    Instr brChk;
    brChk.op = Opcode::Br;
    brChk.type = Type(Type::Kind::Void);
    brChk.labels.push_back("merge");
    brChk.brArgs.emplace_back();
    checked.instructions = {std::move(chk), std::move(brChk)};
    checked.terminated = true;

    BasicBlock merge;
    merge.label = "merge";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    merge.instructions.push_back(std::move(ret));
    merge.terminated = true;

    F.blocks = {std::move(entry), std::move(checked), std::move(merge)};
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::CheckOpt pass;
    pass.run(Fn, manager);

    // Check must not be removed because it does not dominate merge.
    bool found = false;
    for (const auto &I : Fn.blocks[1].instructions)
        if (I.op == Opcode::UDivChk0)
            found = true;
    EXPECT_TRUE(found);
}

#ifndef VIPER_HAS_GTEST
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
