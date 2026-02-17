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
#include "tests/TestHarness.hpp"
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
    // Uses a function-parameter temp as the divisor so that constant-operand
    // elimination does not fire.  This tests the dominance-based redundancy
    // rule: neither sibling block dominates the other, so both checks survive.
    Module M;
    Function F;
    F.name = "siblings";
    F.retType = Type(Type::Kind::Void);

    // Function parameter %0 : i64 — used as divisor (non-constant temp)
    Param divisorParam;
    divisorParam.id = 0;
    divisorParam.type = Type(Type::Kind::I64);
    F.params.push_back(divisorParam);

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
    chkL.operands = {Value::constInt(8), Value::temp(0)}; // divisor is a temp
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
    chkR.operands = {Value::constInt(8), Value::temp(0)}; // same divisor temp
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

TEST(CheckOpt, EliminatesIdxChkWithConstantOperandsInBounds)
{
    // After SCCP runs rewriteConstants(), operands that were proven constant
    // appear as ConstInt literals.  CheckOpt should fold idx.chk(5, 0, 10)
    // at compile time since 0 <= 5 < 10 is trivially true.
    Module M;
    Function F;
    F.name = "const_idxchk";
    F.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    Instr chk;
    chk.result = 0;
    chk.op = Opcode::IdxChk;
    chk.type = Type(Type::Kind::I64);
    chk.operands = {Value::constInt(5), Value::constInt(0), Value::constInt(10)};
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions = {std::move(chk), std::move(ret)};
    entry.terminated = true;

    F.blocks.push_back(std::move(entry));
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::CheckOpt pass;
    pass.run(Fn, manager);

    // Check must be eliminated — index 5 is provably in [0, 10).
    size_t chkCount = 0;
    for (const auto &I : Fn.blocks[0].instructions)
        if (I.op == Opcode::IdxChk)
            ++chkCount;
    EXPECT_EQ(chkCount, 0U);
}

TEST(CheckOpt, EliminatesSDivChk0WithNonZeroConstDivisor)
{
    // sdiv.chk0(lhs, 3) is trivially safe: divisor 3 != 0.
    Module M;
    Function F;
    F.name = "const_sdiv";
    F.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    Instr chk;
    chk.result = 1;
    chk.op = Opcode::SDivChk0;
    chk.type = Type(Type::Kind::I64);
    chk.operands = {Value::constInt(12), Value::constInt(3)};
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions = {std::move(chk), std::move(ret)};
    entry.terminated = true;

    F.blocks.push_back(std::move(entry));
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::CheckOpt pass;
    pass.run(Fn, manager);

    size_t chkCount = 0;
    for (const auto &I : Fn.blocks[0].instructions)
        if (I.op == Opcode::SDivChk0)
            ++chkCount;
    EXPECT_EQ(chkCount, 0U);
}

TEST(CheckOpt, PreservesIdxChkWhenOutOfBounds)
{
    // idx.chk(15, 0, 10) — 15 is NOT in [0, 10) — check must be preserved.
    Module M;
    Function F;
    F.name = "oob_idxchk";
    F.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    Instr chk;
    chk.result = 2;
    chk.op = Opcode::IdxChk;
    chk.type = Type(Type::Kind::I64);
    chk.operands = {Value::constInt(15), Value::constInt(0), Value::constInt(10)};
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions = {std::move(chk), std::move(ret)};
    entry.terminated = true;

    F.blocks.push_back(std::move(entry));
    M.functions.push_back(std::move(F));
    auto &Fn = M.functions.front();

    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);

    il::transform::CheckOpt pass;
    pass.run(Fn, manager);

    size_t chkCount = 0;
    for (const auto &I : Fn.blocks[0].instructions)
        if (I.op == Opcode::IdxChk)
            ++chkCount;
    EXPECT_EQ(chkCount, 1U); // must remain — would trap at runtime
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

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
