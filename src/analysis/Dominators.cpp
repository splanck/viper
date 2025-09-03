// File: src/analysis/Dominators.cpp
// Purpose: Implements dominator tree construction using Cooper's algorithm.
// Key invariants: CFG must represent a single-entry graph.
// Ownership/Lifetime: Uses pointers supplied by CFG.
// Links: docs/dev/analysis.md

#include "analysis/Dominators.hpp"

namespace il::analysis
{

DominatorTree::DominatorTree(const CFG &cfg) : cfg(cfg)
{
    const auto &order = cfg.postorder();
    for (auto *b : order)
        idoms[b] = nullptr;
    if (order.empty())
        return;
    const Block *start = order.back();
    idoms[start] = start;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            const Block *b = *it;
            if (b == start)
                continue;
            const auto &preds = cfg.predecessors(b);
            const Block *newIdom = nullptr;
            for (const Block *p : preds)
            {
                if (idoms[p])
                    newIdom = newIdom ? intersect(p, newIdom) : p;
            }
            if (idoms[b] != newIdom)
            {
                idoms[b] = newIdom;
                changed = true;
            }
        }
    }
}

const il::core::BasicBlock *DominatorTree::idom(const il::core::BasicBlock *b) const
{
    auto it = idoms.find(b);
    return it != idoms.end() ? it->second : nullptr;
}

bool DominatorTree::dominates(const il::core::BasicBlock *a, const il::core::BasicBlock *b) const
{
    if (a == b)
        return true;
    const Block *cur = b;
    while (cur && cur != idoms.at(cur))
    {
        cur = idoms.at(cur);
        if (cur == a)
            return true;
    }
    return false;
}

const il::core::BasicBlock *DominatorTree::intersect(const il::core::BasicBlock *a,
                                                     const il::core::BasicBlock *b) const
{
    const Block *x = a;
    const Block *y = b;
    while (x != y)
    {
        while (cfg.postIndex(x) < cfg.postIndex(y))
            x = idoms.at(x);
        while (cfg.postIndex(y) < cfg.postIndex(x))
            y = idoms.at(y);
    }
    return x;
}

} // namespace il::analysis
