//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_CheckOpt.cpp
// Purpose: Validate that CheckOpt eliminates redundant checks and hoists
//          loop-invariant checks to preheaders.
// Key invariants: Identical checks in dominated blocks are removed; loop-invariant
//                 checks are hoisted when operands are defined outside the loop.
// Ownership/Lifetime: Builds a transient module per test invocation.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/CheckOpt.hpp"
#include "il/transform/LoopSimplify.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace il::core;

namespace
{

BasicBlock *findBlock(Function &function, const std::string &label)
{
    for (auto &block : function.blocks)
    {
        if (block.label == label)
            return &block;
    }
    return nullptr;
}

size_t countIdxChk(const Function &function)
{
    size_t count = 0;
    for (const auto &block : function.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == Opcode::IdxChk)
                ++count;
        }
    }
    return count;
}

il::transform::AnalysisRegistry createRegistry()
{
    il::transform::AnalysisRegistry registry;
    registry.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg", [](Module &mod, Function &fnRef) { return il::transform::buildCFG(mod, fnRef); });
    registry.registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](Module &mod, Function &fnRef)
        {
            viper::analysis::CFGContext ctx(mod);
            return viper::analysis::computeDominatorTree(ctx, fnRef);
        });
    registry.registerFunctionAnalysis<il::transform::LoopInfo>(
        "loop-info",
        [](Module &mod, Function &fnRef) { return il::transform::computeLoopInfo(mod, fnRef); });
    registry.registerFunctionAnalysis<il::transform::LivenessInfo>(
        "liveness",
        [](Module &mod, Function &fnRef) { return il::transform::computeLiveness(mod, fnRef); });
    return registry;
}

/// Test 1: Dominated redundant check elimination
/// Two identical idx.chk in dominated blocks; second should be removed.
void test_redundant_check_elimination()
{
    Module module;
    Function fn;
    fn.name = "test_redundant";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param idxParam{"idx", Type(Type::Kind::I64), nextId++};
    fn.params.push_back(idxParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[idxParam.id] = idxParam.name;

    // entry: %c1 = idx.chk %idx, 0, 10; br then
    BasicBlock entry;
    entry.label = "entry";

    Instr check1;
    check1.result = nextId++;
    check1.op = Opcode::IdxChk;
    check1.type = Type(Type::Kind::I32);
    check1.operands.push_back(Value::temp(idxParam.id)); // index
    check1.operands.push_back(Value::constInt(0));       // lo
    check1.operands.push_back(Value::constInt(10));      // hi
    unsigned check1Id = *check1.result;

    Instr brThen;
    brThen.op = Opcode::Br;
    brThen.type = Type(Type::Kind::Void);
    brThen.labels.push_back("then");
    brThen.brArgs.emplace_back();

    entry.instructions.push_back(std::move(check1));
    entry.instructions.push_back(std::move(brThen));
    entry.terminated = true;

    // then: %c2 = idx.chk %idx, 0, 10; ret %c2 (same check - redundant)
    BasicBlock thenBlock;
    thenBlock.label = "then";

    Instr check2;
    check2.result = nextId++;
    check2.op = Opcode::IdxChk;
    check2.type = Type(Type::Kind::I32);
    check2.operands.push_back(Value::temp(idxParam.id)); // index
    check2.operands.push_back(Value::constInt(0));       // lo
    check2.operands.push_back(Value::constInt(10));      // hi
    unsigned check2Id = *check2.result;

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(check2Id));

    thenBlock.instructions.push_back(std::move(check2));
    thenBlock.instructions.push_back(std::move(ret));
    thenBlock.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(thenBlock));
    fn.valueNames.resize(nextId);

    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

    // Verify we start with 2 idx.chk
    assert(countIdxChk(function) == 2);

    auto registry = createRegistry();
    il::transform::AnalysisManager analysisManager(module, registry);

    il::transform::CheckOpt checkOpt;
    auto preserved = checkOpt.run(function, analysisManager);
    (void)preserved;

    // After optimization, should have only 1 idx.chk
    assert(countIdxChk(function) == 1);

    // The ret should now use check1Id (the dominating check's result)
    BasicBlock *thenResult = findBlock(function, "then");
    assert(thenResult);
    assert(!thenResult->instructions.empty());
    const Instr &retInstr = thenResult->instructions.back();
    assert(retInstr.op == Opcode::Ret);
    assert(!retInstr.operands.empty());
    assert(retInstr.operands[0].kind == Value::Kind::Temp);
    assert(retInstr.operands[0].id == check1Id);
}

/// Test 2: Different checks should not be eliminated
/// Two idx.chk with different bounds; both should remain.
void test_different_checks_not_eliminated()
{
    Module module;
    Function fn;
    fn.name = "test_different";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param idxParam{"idx", Type(Type::Kind::I64), nextId++};
    fn.params.push_back(idxParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[idxParam.id] = idxParam.name;

    // entry: %c1 = idx.chk %idx, 0, 10; br then
    BasicBlock entry;
    entry.label = "entry";

    Instr check1;
    check1.result = nextId++;
    check1.op = Opcode::IdxChk;
    check1.type = Type(Type::Kind::I32);
    check1.operands.push_back(Value::temp(idxParam.id)); // index
    check1.operands.push_back(Value::constInt(0));       // lo
    check1.operands.push_back(Value::constInt(10));      // hi

    Instr brThen;
    brThen.op = Opcode::Br;
    brThen.type = Type(Type::Kind::Void);
    brThen.labels.push_back("then");
    brThen.brArgs.emplace_back();

    entry.instructions.push_back(std::move(check1));
    entry.instructions.push_back(std::move(brThen));
    entry.terminated = true;

    // then: %c2 = idx.chk %idx, 0, 20 (different hi bound); ret %c2
    BasicBlock thenBlock;
    thenBlock.label = "then";

    Instr check2;
    check2.result = nextId++;
    check2.op = Opcode::IdxChk;
    check2.type = Type(Type::Kind::I32);
    check2.operands.push_back(Value::temp(idxParam.id)); // index
    check2.operands.push_back(Value::constInt(0));       // lo
    check2.operands.push_back(Value::constInt(20));      // hi (DIFFERENT)

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*check2.result));

    thenBlock.instructions.push_back(std::move(check2));
    thenBlock.instructions.push_back(std::move(ret));
    thenBlock.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(thenBlock));
    fn.valueNames.resize(nextId);

    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

    // Verify we start with 2 idx.chk
    assert(countIdxChk(function) == 2);

    auto registry = createRegistry();
    il::transform::AnalysisManager analysisManager(module, registry);

    il::transform::CheckOpt checkOpt;
    auto preserved = checkOpt.run(function, analysisManager);
    (void)preserved;

    // After optimization, should still have 2 idx.chk (different bounds)
    assert(countIdxChk(function) == 2);
}

/// Test 3: Loop-invariant check hoisting
/// A loop with idx.chk whose operands are defined outside; should be hoisted.
void test_loop_invariant_hoisting()
{
    Module module;
    Function fn;
    fn.name = "test_loop_hoist";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param idxParam{"idx", Type(Type::Kind::I64), nextId++};
    Param condParam{"cond", Type(Type::Kind::I1), nextId++};
    fn.params.push_back(idxParam);
    fn.params.push_back(condParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[idxParam.id] = idxParam.name;
    fn.valueNames[condParam.id] = condParam.name;

    // entry: cbr %cond, loop, exit
    BasicBlock entry;
    entry.label = "entry";

    Instr entryBr;
    entryBr.op = Opcode::CBr;
    entryBr.type = Type(Type::Kind::Void);
    entryBr.operands.push_back(Value::temp(condParam.id));
    entryBr.labels.push_back("loop");
    entryBr.labels.push_back("exit");
    entryBr.brArgs.emplace_back(); // loop args
    entryBr.brArgs.emplace_back(); // exit args

    entry.instructions.push_back(std::move(entryBr));
    entry.terminated = true;

    // loop: %c = idx.chk %idx, 0, 100; br latch
    // The idx.chk operands (%idx, 0, 100) are all loop-invariant
    BasicBlock loopHeader;
    loopHeader.label = "loop";

    Instr loopCheck;
    loopCheck.result = nextId++;
    loopCheck.op = Opcode::IdxChk;
    loopCheck.type = Type(Type::Kind::I32);
    loopCheck.operands.push_back(Value::temp(idxParam.id)); // invariant
    loopCheck.operands.push_back(Value::constInt(0));       // invariant
    loopCheck.operands.push_back(Value::constInt(100));     // invariant
    unsigned loopCheckId = *loopCheck.result;

    Instr loopBr;
    loopBr.op = Opcode::Br;
    loopBr.type = Type(Type::Kind::Void);
    loopBr.labels.push_back("latch");
    loopBr.brArgs.emplace_back();

    loopHeader.instructions.push_back(std::move(loopCheck));
    loopHeader.instructions.push_back(std::move(loopBr));
    loopHeader.terminated = true;

    // latch: br loop (back edge creates loop)
    BasicBlock latch;
    latch.label = "latch";

    Instr latchBr;
    latchBr.op = Opcode::Br;
    latchBr.type = Type(Type::Kind::Void);
    latchBr.labels.push_back("loop");
    latchBr.brArgs.emplace_back();

    latch.instructions.push_back(std::move(latchBr));
    latch.terminated = true;

    // exit: ret 0
    BasicBlock exit;
    exit.label = "exit";

    Instr retInstr;
    retInstr.op = Opcode::Ret;
    retInstr.type = Type(Type::Kind::Void);
    retInstr.operands.push_back(Value::constInt(0));

    exit.instructions.push_back(std::move(retInstr));
    exit.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(loopHeader));
    fn.blocks.push_back(std::move(latch));
    fn.blocks.push_back(std::move(exit));
    fn.valueNames.resize(nextId);

    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

    auto registry = createRegistry();
    il::transform::AnalysisManager analysisManager(module, registry);

    // First run LoopSimplify to create a preheader
    il::transform::LoopSimplify simplify;
    auto simplifyPreserved = simplify.run(function, analysisManager);
    analysisManager.invalidateAfterFunctionPass(simplifyPreserved, function);

    // Check the loop header has the idx.chk before CheckOpt
    BasicBlock *loopBlock = findBlock(function, "loop");
    assert(loopBlock);
    bool checkInLoop = false;
    for (const auto &instr : loopBlock->instructions)
    {
        if (instr.op == Opcode::IdxChk)
        {
            checkInLoop = true;
            break;
        }
    }
    assert(checkInLoop && "idx.chk should be in loop header before CheckOpt");

    // Run CheckOpt to hoist invariant checks
    il::transform::CheckOpt checkOpt;
    auto checkOptPreserved = checkOpt.run(function, analysisManager);
    (void)checkOptPreserved;

    // After CheckOpt, the idx.chk should be in the preheader, not the loop
    BasicBlock *preheader = findBlock(function, "loop.preheader");
    assert(preheader && "LoopSimplify should have created a preheader");

    bool checkInPreheader = false;
    for (const auto &instr : preheader->instructions)
    {
        if (instr.op == Opcode::IdxChk && instr.result && *instr.result == loopCheckId)
        {
            checkInPreheader = true;
            break;
        }
    }
    assert(checkInPreheader && "idx.chk should be hoisted to preheader");

    // Verify check is no longer in the loop header
    loopBlock = findBlock(function, "loop");
    assert(loopBlock);
    for (const auto &instr : loopBlock->instructions)
    {
        assert(instr.op != Opcode::IdxChk && "idx.chk should not be in loop after hoisting");
    }
}

} // namespace

int main()
{
    test_redundant_check_elimination();
    test_different_checks_not_eliminated();
    test_loop_invariant_hoisting();
    return 0;
}
