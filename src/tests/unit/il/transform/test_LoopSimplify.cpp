//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_LoopSimplify.cpp
// Purpose: Validate that LoopSimplify inserts preheaders and preserves arguments. 
// Key invariants: Entry edge splits into a dedicated preheader with forwarded SSA values.
// Ownership/Lifetime: Builds a local module for the duration of the test run.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/LoopSimplify.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
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

} // namespace

int main()
{
    Module module;
    Function fn;
    fn.name = "loop_preheader";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param condParam{"cond", Type(Type::Kind::I1), nextId++};
    fn.params.push_back(condParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[condParam.id] = condParam.name;

    BasicBlock entry;
    entry.label = "entry";
    Instr entryBranch;
    entryBranch.op = Opcode::CBr;
    entryBranch.type = Type(Type::Kind::Void);
    entryBranch.operands.push_back(Value::temp(condParam.id));
    entryBranch.labels.push_back("loop");
    entryBranch.labels.push_back("exit");
    entryBranch.brArgs.emplace_back(std::vector<Value>{Value::constInt(0)});
    entryBranch.brArgs.emplace_back(std::vector<Value>{Value::constInt(42)});
    entry.instructions.push_back(std::move(entryBranch));
    entry.terminated = true;

    BasicBlock loopHeader;
    loopHeader.label = "loop";
    Param loopParam{"acc", Type(Type::Kind::I64), nextId++};
    loopHeader.params.push_back(loopParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[loopParam.id] = loopParam.name;

    Instr headerBranch;
    headerBranch.op = Opcode::Br;
    headerBranch.type = Type(Type::Kind::Void);
    headerBranch.labels.push_back("latch");
    headerBranch.brArgs.emplace_back(std::vector<Value>{Value::temp(loopParam.id)});
    loopHeader.instructions.push_back(std::move(headerBranch));
    loopHeader.terminated = true;

    BasicBlock latch;
    latch.label = "latch";
    Param latchParam{"next", Type(Type::Kind::I64), nextId++};
    latch.params.push_back(latchParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[latchParam.id] = latchParam.name;

    Instr latchBranch;
    latchBranch.op = Opcode::Br;
    latchBranch.type = Type(Type::Kind::Void);
    latchBranch.labels.push_back("loop");
    latchBranch.brArgs.emplace_back(std::vector<Value>{Value::temp(latchParam.id)});
    latch.instructions.push_back(std::move(latchBranch));
    latch.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    Param exitParam{"result", Type(Type::Kind::I64), nextId++};
    exit.params.push_back(exitParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[exitParam.id] = exitParam.name;

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(exitParam.id));
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(loopHeader));
    fn.blocks.push_back(std::move(latch));
    fn.blocks.push_back(std::move(exit));

    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

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

    il::transform::AnalysisManager analysisManager(module, registry);

    il::transform::LoopSimplify pass;
    il::transform::PreservedAnalyses preserved = pass.run(function, analysisManager);
    (void)preserved;

    const BasicBlock *preheader = findBlock(function, "loop.preheader");
    assert(preheader && "LoopSimplify must create a dedicated preheader block");
    assert(preheader->params.size() == function.blocks[1].params.size());

    const BasicBlock *entryBlock = findBlock(function, "entry");
    assert(entryBlock);
    assert(entryBlock->terminated);
    assert(!entryBlock->instructions.empty());
    const Instr &entryTerm = entryBlock->instructions.back();
    assert(entryTerm.op == Opcode::CBr);
    assert(entryTerm.labels.size() == 2);
    assert(entryTerm.labels.front() == preheader->label);
    assert(entryTerm.brArgs.size() == 2);
    assert(entryTerm.brArgs.front().size() == 1);
    const Value &entryForward = entryTerm.brArgs.front().front();
    assert(entryForward.kind == Value::Kind::ConstInt);
    assert(entryForward.i64 == 0);

    assert(preheader->terminated);
    assert(!preheader->instructions.empty());
    const Instr &preheaderTerm = preheader->instructions.back();
    assert(preheaderTerm.op == Opcode::Br);
    assert(preheaderTerm.labels.size() == 1);
    assert(preheaderTerm.labels.front() == function.blocks[1].label);
    if (!preheader->params.empty())
    {
        assert(preheaderTerm.brArgs.size() == 1);
        assert(preheaderTerm.brArgs.front().size() == preheader->params.size());
        const Value &forwardedValue = preheaderTerm.brArgs.front().front();
        assert(forwardedValue.kind == Value::Kind::Temp);
        assert(forwardedValue.id == preheader->params.front().id);
    }

    return 0;
}
