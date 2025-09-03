// File: src/il/analysis/Dominators.cpp
// Purpose: Implement dominator tree construction.
// Key invariants: Follows Cooper et al.'s iterative algorithm.
// Ownership/Lifetime: Uses CFG; no ownership transfer.
// Links: docs/class-catalog.md

#include "il/analysis/Dominators.hpp"

using namespace il::core;

namespace il::analysis
{

DominatorTree::DominatorTree(const CFG &cfg) : cfg_(cfg)
{
    const auto &order = cfg_.rpo();
    if (order.empty())
        return;
    for (unsigned i = 0; i < order.size(); ++i)
        rpoIndex_[order[i]] = i;
    const BasicBlock *start = order.front();
    idom_[start] = start;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t i = 1; i < order.size(); ++i)
        {
            const BasicBlock *b = order[i];
            const BasicBlock *newIdom = nullptr;
            for (const BasicBlock *p : cfg_.preds(*b))
            {
                if (idom_.count(p))
                {
                    newIdom = p;
                    break;
                }
            }
            if (!newIdom)
                continue;
            for (const BasicBlock *p : cfg_.preds(*b))
            {
                if (p == newIdom || !idom_.count(p))
                    continue;
                newIdom = intersect(p, newIdom);
            }
            if (!idom_.count(b) || idom_[b] != newIdom)
            {
                idom_[b] = newIdom;
                changed = true;
            }
        }
    }
}

const BasicBlock *DominatorTree::idom(const BasicBlock &b) const
{
    auto it = idom_.find(&b);
    if (it == idom_.end() || it->second == &b)
        return nullptr;
    return it->second;
}

bool DominatorTree::dominates(const BasicBlock &a, const BasicBlock &b) const
{
    if (&a == &b)
        return true;
    const BasicBlock *cur = &b;
    while (cur && cur != &a)
    {
        auto it = idom_.find(cur);
        if (it == idom_.end() || it->second == cur)
            return false;
        cur = it->second;
    }
    return cur == &a;
}

const BasicBlock *DominatorTree::intersect(const BasicBlock *a, const BasicBlock *b) const
{
    while (a != b)
    {
        while (rpoIndex_.at(a) > rpoIndex_.at(b))
            a = idom_.at(a);
        while (rpoIndex_.at(b) > rpoIndex_.at(a))
            b = idom_.at(b);
    }
    return a;
}

} // namespace il::analysis
