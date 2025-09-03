// File: src/il/analysis/CFG.cpp
// Purpose: Implement basic control-flow graph utilities for functions.
// Key invariants: Successor/predecessor lists are constructed from terminators.
// Ownership/Lifetime: References remain valid as long as the function lives.
// Links: docs/class-catalog.md

#include "il/analysis/CFG.hpp"
#include "il/core/Instr.hpp"
#include <algorithm>
#include <limits>

using namespace il::core;

namespace il::analysis
{

CFG::CFG(const Function &f)
{
    // Map labels to blocks for quick lookup.
    std::unordered_map<std::string, const BasicBlock *> labelMap;
    for (const auto &b : f.blocks)
        labelMap[b.label] = &b;

    // Build successors and predecessors.
    for (const auto &b : f.blocks)
    {
        if (!b.terminated || b.instructions.empty())
            continue;
        const Instr &term = b.instructions.back();
        for (const auto &lab : term.labels)
        {
            auto it = labelMap.find(lab);
            if (it == labelMap.end())
                continue;
            succs_[&b].push_back(it->second);
            preds_[it->second].push_back(&b);
        }
    }

    // Depth-first search for post-order.
    std::unordered_set<const BasicBlock *> visited;
    if (!f.blocks.empty())
        dfs(&f.blocks.front(), visited);
    for (unsigned i = 0; i < postOrder_.size(); ++i)
        postIndex_[postOrder_[i]] = i;
    rpo_ = postOrder_;
    std::reverse(rpo_.begin(), rpo_.end());
}

const std::vector<const BasicBlock *> &CFG::succs(const BasicBlock &b) const
{
    static const std::vector<const BasicBlock *> empty;
    auto it = succs_.find(&b);
    return it != succs_.end() ? it->second : empty;
}

const std::vector<const BasicBlock *> &CFG::preds(const BasicBlock &b) const
{
    static const std::vector<const BasicBlock *> empty;
    auto it = preds_.find(&b);
    return it != preds_.end() ? it->second : empty;
}

unsigned CFG::postOrder(const BasicBlock &b) const
{
    auto it = postIndex_.find(&b);
    return it != postIndex_.end() ? it->second : std::numeric_limits<unsigned>::max();
}

void CFG::dfs(const BasicBlock *b, std::unordered_set<const BasicBlock *> &visited)
{
    if (visited.count(b))
        return;
    visited.insert(b);
    auto it = succs_.find(b);
    if (it != succs_.end())
        for (const BasicBlock *s : it->second)
            dfs(s, visited);
    postOrder_.push_back(b);
}

} // namespace il::analysis
