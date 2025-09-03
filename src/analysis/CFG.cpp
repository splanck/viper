// File: src/analysis/CFG.cpp
// Purpose: Builds basic control-flow graph information.
// Key invariants: Predecessor/successor lists mirror terminator labels.
// Ownership/Lifetime: Operates on existing function; no ownership.
// Links: docs/dev/analysis.md

#include "analysis/CFG.hpp"
#include "il/core/Instr.hpp"
#include <functional>
#include <unordered_set>

using namespace il::core;

namespace il::analysis
{

CFG::CFG(const Function &fn)
{
    compute(fn);
}

void CFG::compute(const Function &fn)
{
    std::unordered_map<std::string, const BasicBlock *> labelMap;
    for (const auto &bb : fn.blocks)
    {
        labelMap[bb.label] = &bb;
        succ[&bb];
        pred[&bb];
    }

    for (const auto &bb : fn.blocks)
    {
        if (bb.instructions.empty())
            continue;
        const Instr &term = bb.instructions.back();
        for (const auto &lbl : term.labels)
        {
            auto it = labelMap.find(lbl);
            if (it != labelMap.end())
            {
                succ[&bb].push_back(it->second);
                pred[it->second].push_back(&bb);
            }
        }
    }

    std::unordered_set<const BasicBlock *> visited;
    std::function<void(const BasicBlock *)> dfs = [&](const BasicBlock *b)
    {
        if (!visited.insert(b).second)
            return;
        for (const BasicBlock *s : succ[b])
            dfs(s);
        postIndex[b] = postorder.size();
        postorder.push_back(b);
    };

    if (!fn.blocks.empty())
        dfs(&fn.blocks.front());
}

const std::vector<const BasicBlock *> &CFG::successors(const BasicBlock &bb) const
{
    return succ.at(&bb);
}

const std::vector<const BasicBlock *> &CFG::predecessors(const BasicBlock &bb) const
{
    return pred.at(&bb);
}

size_t CFG::postorderIndex(const BasicBlock &bb) const
{
    auto it = postIndex.find(&bb);
    return it == postIndex.end() ? static_cast<size_t>(-1) : it->second;
}

} // namespace il::analysis
