//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a lightweight loop-invariant code motion pass.  The pass hoists
// trivially safe instructions—those proven non-trapping, side-effect free, and
// operand-invariant—into the loop preheader so they execute only once before the
// loop body.  It relies on LoopSimplify to guarantee the presence of a
// dedicated preheader and uses dominance information to traverse blocks in a
// stable order.
//
//===----------------------------------------------------------------------===//

#include "il/transform/LICM.hpp"

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/Dominators.hpp"
#include "il/utils/Utils.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "il/verify/VerifierTable.hpp"

#include <string>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform
{
namespace
{
bool isSafeToHoist(const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (info.isTerminator || info.hasSideEffects)
        return false;
    if (!instr.labels.empty() || !instr.brArgs.empty())
        return false;

    auto spec = verify::lookupSpec(instr.op);
    if (!spec || spec->hasSideEffects)
        return false;

    auto props = verify::lookup(instr.op);
    if (!props || props->canTrap)
        return false;

    if (memoryEffects(instr.op) != MemoryEffects::None)
        return false;

    return true;
}

BasicBlock *findBlock(Function &function, const std::string &label)
{
    return viper::il::findBlock(function, label);
}

BasicBlock *findPreheader(Function &function, const Loop &loop, BasicBlock &header)
{
    BasicBlock *preheader = nullptr;
    for (auto &block : function.blocks)
    {
        if (loop.contains(block.label))
            continue;
        if (!block.terminated || block.instructions.empty())
            continue;
        const Instr &term = block.instructions.back();
        bool targetsHeader = false;
        for (const auto &label : term.labels)
        {
            if (label == header.label)
            {
                targetsHeader = true;
                break;
            }
        }
        if (!targetsHeader)
            continue;

        if (preheader && preheader != &block)
            return nullptr;
        preheader = &block;
    }
    return preheader;
}

void seedInvariants(const Loop &loop, Function &function, std::unordered_set<unsigned> &invariants)
{
    for (const auto &param : function.params)
        invariants.insert(param.id);

    for (auto &block : function.blocks)
    {
        if (loop.contains(block.label))
            continue;
        for (const auto &param : block.params)
            invariants.insert(param.id);
        for (const auto &instr : block.instructions)
            if (instr.result)
                invariants.insert(*instr.result);
    }
}

bool operandsInvariant(const Instr &instr, const std::unordered_set<unsigned> &invariants)
{
    auto isInvariantValue = [&invariants](const Value &value)
    {
        if (value.kind != Value::Kind::Temp)
            return true;
        return invariants.count(value.id) > 0;
    };

    for (const auto &operand : instr.operands)
    {
        if (!isInvariantValue(operand))
            return false;
    }

    for (const auto &argList : instr.brArgs)
        for (const auto &arg : argList)
            if (!isInvariantValue(arg))
                return false;

    return true;
}

void collectDominanceOrder(BasicBlock *block,
                           const Loop &loop,
                           const viper::analysis::DomTree &domTree,
                           std::vector<BasicBlock *> &order)
{
    if (!block)
        return;

    order.push_back(block);

    auto it = domTree.children.find(block);
    if (it == domTree.children.end())
        return;

    for (auto *child : it->second)
    {
        if (!loop.contains(child->label))
            continue;
        collectDominanceOrder(child, loop, domTree, order);
    }
}

} // namespace

std::string_view LICM::id() const
{
    return "licm";
}

PreservedAnalyses LICM::run(Function &function, AnalysisManager &analysis)
{
    auto &domTree = analysis.getFunctionResult<viper::analysis::DomTree>("dominators", function);
    auto &loopInfo = analysis.getFunctionResult<LoopInfo>("loop-info", function);

    bool changed = false;

    for (const Loop &loop : loopInfo.loops())
    {
        BasicBlock *header = findBlock(function, loop.headerLabel);
        if (!header)
            continue;

        BasicBlock *preheader = findPreheader(function, loop, *header);
        if (!preheader)
            continue;

        std::unordered_set<unsigned> invariants;
        invariants.reserve(32);
        seedInvariants(loop, function, invariants);

        std::vector<BasicBlock *> blockOrder;
        blockOrder.reserve(loop.blockLabels.size());
        collectDominanceOrder(header, loop, domTree, blockOrder);

        for (BasicBlock *block : blockOrder)
        {
            for (std::size_t idx = 0; idx < block->instructions.size();)
            {
                Instr &instr = block->instructions[idx];
                if (!isSafeToHoist(instr) || !operandsInvariant(instr, invariants))
                {
                    ++idx;
                    continue;
                }

                Instr hoisted = std::move(instr);
                block->instructions.erase(block->instructions.begin() + idx);

                std::size_t insertIndex = preheader->instructions.size();
                if (preheader->terminated && insertIndex > 0)
                    --insertIndex;
                auto inserted = preheader->instructions.insert(
                    preheader->instructions.begin() + insertIndex, std::move(hoisted));

                Instr &insertedInstr = *inserted;
                if (insertedInstr.result)
                    invariants.insert(*insertedInstr.result);

                changed = true;
            }
        }
    }

    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    preserved.preserveFunction("cfg");
    preserved.preserveFunction("dominators");
    preserved.preserveFunction("loop-info");
    return preserved;
}

} // namespace il::transform
