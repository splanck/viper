// File: src/analysis/CFG.cpp
// Purpose: Implements construction of basic block predecessor/successor lists.
// Key invariants: All branch targets map to known blocks.
// Ownership/Lifetime: CFG stores non-owning pointers.
// Links: docs/dev/analysis.md

#include "analysis/CFG.hpp"
#include <functional>
#include <unordered_set>

namespace il::analysis
{

CFG::CFG(const il::core::Function &f) : func(f)
{
    using Block = il::core::BasicBlock;
    std::unordered_map<std::string, const Block *> labelToBlock;
    for (const auto &b : f.blocks)
    {
        labelToBlock[b.label] = &b;
        preds[&b];
        succs[&b];
    }
    for (const auto &b : f.blocks)
    {
        if (!b.terminated || b.instructions.empty())
            continue;
        const auto &term = b.instructions.back();
        if (term.op == il::core::Opcode::Br || term.op == il::core::Opcode::CBr)
        {
            for (const auto &lab : term.labels)
            {
                auto it = labelToBlock.find(lab);
                if (it != labelToBlock.end())
                {
                    succs[&b].push_back(it->second);
                    preds[it->second].push_back(&b);
                }
            }
        }
    }
    const Block *entry = f.blocks.empty() ? nullptr : &f.blocks.front();
    std::unordered_set<const Block *> visited;
    std::function<void(const Block *)> dfs = [&](const Block *bb)
    {
        if (visited.count(bb))
            return;
        visited.insert(bb);
        for (const Block *s : succs[bb])
            dfs(s);
        postOrder.push_back(bb);
    };
    if (entry)
        dfs(entry);
    for (std::size_t i = 0; i < postOrder.size(); ++i)
        postNum[postOrder[i]] = i;
}

const std::vector<const il::core::BasicBlock *> &CFG::predecessors(
    const il::core::BasicBlock *bb) const
{
    return preds.at(bb);
}

const std::vector<const il::core::BasicBlock *> &CFG::successors(
    const il::core::BasicBlock *bb) const
{
    return succs.at(bb);
}

std::size_t CFG::postIndex(const il::core::BasicBlock *bb) const
{
    return postNum.at(bb);
}

} // namespace il::analysis
