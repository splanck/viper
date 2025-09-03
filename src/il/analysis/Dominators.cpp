// File: src/il/analysis/Dominators.cpp
// Purpose: Implements a simple set-based dominator tree.
// Key invariants: Dominator sets converge for finite CFGs.
// Ownership/Lifetime: Uses non-owning pointers to blocks.
// Links: docs/dev/analysis.md

#include "il/analysis/Dominators.hpp"

namespace il::analysis
{

DominatorTree::DominatorTree(const CFG &cfg) : blocks_(cfg.postOrder())
{
    for (std::size_t i = 0; i < blocks_.size(); ++i)
        index_[blocks_[i]] = i;
    if (blocks_.empty())
        return;
    const auto *entry = blocks_.back();
    for (const auto *b : blocks_)
    {
        if (b == entry)
            doms_[b] = {b};
        else
            doms_[b].insert(blocks_.begin(), blocks_.end());
    }
    bool changed;
    int iter = 0;
    const int kMaxIter = 1000;
    do
    {
        changed = false;
        for (const auto *b : blocks_)
        {
            if (b == entry)
                continue;
            std::unordered_set<const il::core::BasicBlock *> newSet{b};
            bool first = true;
            for (const auto *p : cfg.predecessors(*b))
            {
                if (first)
                {
                    newSet.insert(doms_[p].begin(), doms_[p].end());
                    first = false;
                }
                else
                {
                    std::unordered_set<const il::core::BasicBlock *> tmp;
                    for (const auto *x : newSet)
                        if (doms_[p].count(x))
                            tmp.insert(x);
                    newSet.swap(tmp);
                }
            }
            if (newSet != doms_[b])
            {
                doms_[b] = std::move(newSet);
                changed = true;
            }
        }
        ++iter;
    } while (changed && iter < kMaxIter);
    for (const auto *b : blocks_)
    {
        if (b == entry)
        {
            idom_[b] = b;
            continue;
        }
        std::size_t best = 0;
        const il::core::BasicBlock *bestBlk = entry;
        for (const auto *d : doms_[b])
        {
            if (d == b)
                continue;
            std::size_t idx = index_[d];
            if (idx >= best)
            {
                best = idx;
                bestBlk = d;
            }
        }
        idom_[b] = bestBlk;
    }
}

const il::core::BasicBlock *DominatorTree::idom(const il::core::BasicBlock &bb) const
{
    auto it = idom_.find(&bb);
    if (it == idom_.end())
        return nullptr;
    return it->second;
}

bool DominatorTree::dominates(const il::core::BasicBlock &a, const il::core::BasicBlock &b) const
{
    auto it = doms_.find(&b);
    if (it == doms_.end())
        return false;
    return it->second.count(&a) != 0;
}

} // namespace il::analysis
