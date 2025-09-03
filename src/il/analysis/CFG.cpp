// File: src/il/analysis/CFG.cpp
// Purpose: Implements basic control-flow graph construction.
// Key invariants: Graph reflects terminator labels in order.
// Ownership/Lifetime: Holds non-owning pointers to blocks.
// Links: docs/dev/analysis.md

#include "il/analysis/CFG.hpp"
#include <string>

#include <functional>
#include <unordered_set>

namespace il::analysis
{

CFG::CFG(const il::core::Function &f)
{
    std::unordered_map<std::string, const il::core::BasicBlock *> labels;
    for (const auto &b : f.blocks)
    {
        labels[b.label] = &b;
        succ_[&b];
        pred_[&b];
    }
    for (const auto &b : f.blocks)
    {
        if (!b.terminated || b.instructions.empty())
            continue;
        const auto &term = b.instructions.back();
        if (term.op == il::core::Opcode::Br || term.op == il::core::Opcode::CBr)
        {
            for (const auto &lbl : term.labels)
            {
                auto it = labels.find(lbl);
                if (it != labels.end())
                {
                    succ_[&b].push_back(it->second);
                    pred_[it->second].push_back(&b);
                }
            }
        }
    }
    std::unordered_set<const il::core::BasicBlock *> visited;
    std::function<void(const il::core::BasicBlock *)> dfs = [&](const il::core::BasicBlock *bb)
    {
        if (!bb || visited.count(bb))
            return;
        visited.insert(bb);
        for (const auto *s : succ_[bb])
            dfs(s);
        post_.push_back(bb);
    };
    if (!f.blocks.empty())
        dfs(&f.blocks.front());
}

const std::vector<const il::core::BasicBlock *> &CFG::successors(
    const il::core::BasicBlock &bb) const
{
    auto it = succ_.find(&bb);
    return it->second;
}

const std::vector<const il::core::BasicBlock *> &CFG::predecessors(
    const il::core::BasicBlock &bb) const
{
    auto it = pred_.find(&bb);
    return it->second;
}

const std::vector<const il::core::BasicBlock *> &CFG::postOrder() const
{
    return post_;
}

} // namespace il::analysis
