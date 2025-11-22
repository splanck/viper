//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_LICM.cpp
// Purpose: Validate that LICM hoists trivially safe, loop-invariant instructions. 
// Key invariants: LoopSimplify provides a preheader and LICM moves invariant math there.
// Ownership/Lifetime: Builds a transient module per test invocation.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/LICM.hpp"
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
    fn.name = "licm_invariant";
    fn.retType = Type(Type::Kind::F64);

    unsigned nextId = 0;
    Param condParam{"cond", Type(Type::Kind::I1), nextId++};
    Param seedParam{"seed", Type(Type::Kind::F64), nextId++};
    fn.params.push_back(condParam);
    fn.params.push_back(seedParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[condParam.id] = condParam.name;
    fn.valueNames[seedParam.id] = seedParam.name;

    BasicBlock entry;
    entry.label = "entry";
    Instr entryBranch;
    entryBranch.op = Opcode::CBr;
    entryBranch.type = Type(Type::Kind::Void);
    entryBranch.operands.push_back(Value::temp(condParam.id));
    entryBranch.labels.push_back("loop");
    entryBranch.labels.push_back("exit");
    entryBranch.brArgs.emplace_back(std::vector<Value>{Value::temp(seedParam.id)});
    entryBranch.brArgs.emplace_back(std::vector<Value>{Value::temp(seedParam.id)});
    entry.instructions.push_back(std::move(entryBranch));
    entry.terminated = true;

    BasicBlock loopHeader;
    loopHeader.label = "loop";
    Param loopParam{"acc", Type(Type::Kind::F64), nextId++};
    loopHeader.params.push_back(loopParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[loopParam.id] = loopParam.name;

    Instr invariant;
    invariant.result = nextId++;
    invariant.op = Opcode::FAdd;
    invariant.type = Type(Type::Kind::F64);
    invariant.operands.push_back(Value::constFloat(7.0));
    invariant.operands.push_back(Value::constFloat(5.0));
    unsigned invariantId = *invariant.result;

    Instr combine;
    combine.result = nextId++;
    combine.op = Opcode::FAdd;
    combine.type = Type(Type::Kind::F64);
    combine.operands.push_back(Value::temp(invariantId));
    combine.operands.push_back(Value::temp(loopParam.id));

    Instr headerBranch;
    headerBranch.op = Opcode::Br;
    headerBranch.type = Type(Type::Kind::Void);
    headerBranch.labels.push_back("latch");
    headerBranch.brArgs.emplace_back(std::vector<Value>{Value::temp(*combine.result)});

    loopHeader.instructions.push_back(std::move(invariant));
    loopHeader.instructions.push_back(std::move(combine));
    loopHeader.instructions.push_back(std::move(headerBranch));
    loopHeader.terminated = true;

    BasicBlock latch;
    latch.label = "latch";
    Param latchParam{"next", Type(Type::Kind::F64), nextId++};
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
    Param exitParam{"result", Type(Type::Kind::F64), nextId++};
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
    registry.registerFunctionAnalysis<il::transform::LivenessInfo>(
        "liveness",
        [](Module &mod, Function &fnRef) { return il::transform::computeLiveness(mod, fnRef); });

    il::transform::AnalysisManager analysisManager(module, registry);

    il::transform::LoopSimplify simplify;
    il::transform::PreservedAnalyses simplifyPreserved = simplify.run(function, analysisManager);
    analysisManager.invalidateAfterFunctionPass(simplifyPreserved, function);

    il::transform::LICM licm;
    il::transform::PreservedAnalyses licmPreserved = licm.run(function, analysisManager);
    (void)licmPreserved;

    BasicBlock *preheader = findBlock(function, "loop.preheader");
    assert(preheader && "LICM expects LoopSimplify to provide a preheader");
    assert(preheader->terminated);
    const Instr *hoistedInstr = nullptr;
    for (const Instr &candidate : preheader->instructions)
    {
        if (candidate.result && *candidate.result == invariantId)
        {
            hoistedInstr = &candidate;
            break;
        }
    }
    assert(hoistedInstr && "Hoisted instruction must appear in the preheader");
    assert(hoistedInstr->op == Opcode::FAdd);
    assert(hoistedInstr->operands.size() == 2);
    assert(hoistedInstr->operands[0].kind == Value::Kind::ConstFloat);
    assert(hoistedInstr->operands[1].kind == Value::Kind::ConstFloat);

    BasicBlock *loopBlock = findBlock(function, "loop");
    assert(loopBlock);
    for (const Instr &I : loopBlock->instructions)
    {
        if (!I.result)
            continue;
        assert(*I.result != invariantId && "Hoisted instruction must leave the loop body");
    }

    BasicBlock *latchBlock = findBlock(function, "latch");
    assert(latchBlock);
    const Instr &latchTerm = latchBlock->instructions.back();
    assert(latchTerm.op == Opcode::Br);
    assert(latchTerm.brArgs.size() == 1);
    assert(latchTerm.brArgs.front().size() == 1);
    const Value &forwarded = latchTerm.brArgs.front().front();
    assert(forwarded.kind == Value::Kind::Temp);
    assert(forwarded.id == latchBlock->params.front().id);

    return 0;
}
