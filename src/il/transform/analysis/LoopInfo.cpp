//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lightweight loop detection helpers derived from CFG and dominator
// information.  The analysis records loop membership by label to remain stable
// even when passes insert additional blocks after the summary was computed.
//
//===----------------------------------------------------------------------===//

#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/utils/Utils.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <unordered_map>

using namespace il::core;

namespace il::transform
{

bool Loop::contains(std::string_view label) const
{
    // Heterogeneous lookup - no temporary std::string allocation
    return members_.find(label) != members_.end();
}

void Loop::finalize()
{
    members_.clear();
    members_.reserve(blockLabels.size());
    for (const auto &label : blockLabels)
        members_.insert(label);
}

const Loop *LoopInfo::findLoop(std::string_view headerLabel) const
{
    for (const auto &loop : loops_)
    {
        if (loop.headerLabel == headerLabel)
            return &loop;
    }
    return nullptr;
}

void LoopInfo::addLoop(Loop loop)
{
    loop.finalize();
    loops_.push_back(std::move(loop));
}

const Loop *LoopInfo::parent(const Loop &loop) const
{
    if (loop.parentHeader.empty())
        return nullptr;
    return findLoop(loop.parentHeader);
}

namespace
{
std::vector<BasicBlock *> getPredecessors(const viper::analysis::CFGContext &ctx, BasicBlock &block)
{
    auto it = ctx.blockPredecessors.find(&block);
    if (it != ctx.blockPredecessors.end())
    {
        std::vector<BasicBlock *> result;
        result.reserve(it->second.size());
        for (auto *pred : it->second)
            result.push_back(const_cast<BasicBlock *>(pred));
        return result;
    }
    return {};
}

std::vector<BasicBlock *> getPredecessors(const viper::analysis::CFGContext &ctx,
                                          const BasicBlock &block)
{
    return getPredecessors(ctx, const_cast<BasicBlock &>(block));
}

} // namespace

LoopInfo computeLoopInfo(Module &module, Function &function)
{
    LoopInfo info;

    viper::analysis::CFGContext cfgCtx(module);
    viper::analysis::DomTree domTree = viper::analysis::computeDominatorTree(cfgCtx, function);

    // Discover loops (header, body, latches).
    for (auto &block : function.blocks)
    {
        std::vector<BasicBlock *> latchBlocks;
        for (BasicBlock *pred : getPredecessors(cfgCtx, block))
        {
            if (domTree.dominates(&block, pred))
                latchBlocks.push_back(pred);
        }

        if (latchBlocks.empty())
            continue;

        Loop loop;
        loop.headerLabel = block.label;
        loop.blockLabels.push_back(block.label);

        std::deque<BasicBlock *> worklist;
        std::unordered_map<BasicBlock *, bool> visited;
        visited[&block] = true;

        for (BasicBlock *latch : latchBlocks)
        {
            if (!visited[latch])
            {
                worklist.push_back(latch);
                visited[latch] = true;
            }
            loop.blockLabels.push_back(latch->label);
            loop.latchLabels.push_back(latch->label);
        }

        while (!worklist.empty())
        {
            BasicBlock *current = worklist.front();
            worklist.pop_front();

            for (BasicBlock *pred : getPredecessors(cfgCtx, *current))
            {
                if (!domTree.dominates(&block, pred))
                    continue;
                if (visited[pred])
                    continue;
                visited[pred] = true;
                worklist.push_back(pred);
                loop.blockLabels.push_back(pred->label);
            }
        }

        info.addLoop(std::move(loop));
    }

    // Parent/child nesting (pick smallest containing loop as parent).
    auto contains = [](const Loop &loop, std::string_view label) { return loop.contains(label); };
    for (auto &loop : info.loops_)
    {
        std::optional<std::string> parent;
        for (const auto &candidate : info.loops_)
        {
            if (candidate.headerLabel == loop.headerLabel)
                continue;
            if (!contains(candidate, loop.headerLabel))
                continue;
            if (!parent ||
                info.findLoop(*parent)->blockLabels.size() > candidate.blockLabels.size())
            {
                parent = candidate.headerLabel;
            }
        }
        if (parent)
        {
            loop.parentHeader = *parent;
        }
    }
    // Populate children lists
    for (auto &loop : info.loops_)
    {
        if (loop.parentHeader.empty())
            continue;
        if (auto *parentLoop = info.findLoop(loop.parentHeader))
        {
            auto *mutableParent = const_cast<Loop *>(parentLoop);
            mutableParent->childHeaders.push_back(loop.headerLabel);
        }
    }

    // Exits: edges from loop body to outside.
    for (auto &loop : info.loops_)
    {
        std::vector<LoopExit> exits;
        for (const auto &label : loop.blockLabels)
        {
            BasicBlock *block = ::viper::il::findBlock(function, label);
            if (!block || block->instructions.empty())
                continue;
            const Instr &term = block->instructions.back();
            for (const auto &succ : term.labels)
            {
                if (!loop.contains(succ))
                    exits.push_back(LoopExit{label, succ});
            }
        }
        loop.exits = std::move(exits);
    }

    return info;
}

} // namespace il::transform
