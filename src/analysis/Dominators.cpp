// File: src/analysis/Dominators.cpp
// Purpose: Implements simple Cooper et al. dominator algorithm.
// Key invariants: Root dominates all; idom map stable.
// Ownership/Lifetime: Uses CFG; no ownership of blocks.
// Links: docs/dev/analysis.md

#include "analysis/Dominators.hpp"
#include <vector>

using namespace il::core;

namespace il::analysis
{
namespace
{
const BasicBlock *intersect(const BasicBlock *b1,
                            const BasicBlock *b2,
                            const std::unordered_map<const BasicBlock *, const BasicBlock *> &idom,
                            const std::unordered_map<const BasicBlock *, size_t> &rpoIdx)
{
    while (b1 != b2)
    {
        while (rpoIdx.at(b1) > rpoIdx.at(b2))
            b1 = idom.at(b1);
        while (rpoIdx.at(b2) > rpoIdx.at(b1))
            b2 = idom.at(b2);
    }
    return b1;
}
} // namespace

DominatorTree::DominatorTree(const CFG &cfg)
{
    const auto &po = cfg.postorderBlocks();
    if (po.empty())
        return;
    std::vector<const BasicBlock *> rpo(po.rbegin(), po.rend());
    std::unordered_map<const BasicBlock *, size_t> rpoIdx;
    for (size_t i = 0; i < rpo.size(); ++i)
        rpoIdx[rpo[i]] = i;

    const BasicBlock *start = rpo.front();
    idoms[start] = start;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t i = 1; i < rpo.size(); ++i)
        {
            const BasicBlock *b = rpo[i];
            const auto &preds = cfg.predecessors(*b);
            const BasicBlock *newIdom = nullptr;
            for (const BasicBlock *p : preds)
            {
                if (idoms.count(p))
                {
                    newIdom = p;
                    break;
                }
            }
            if (!newIdom)
                continue;
            for (const BasicBlock *p : preds)
            {
                if (p == newIdom)
                    continue;
                if (idoms.count(p))
                    newIdom = intersect(p, newIdom, idoms, rpoIdx);
            }
            if (idoms[b] != newIdom)
            {
                idoms[b] = newIdom;
                changed = true;
            }
        }
    }
}

const BasicBlock *DominatorTree::idom(const BasicBlock &bb) const
{
    auto it = idoms.find(&bb);
    if (it == idoms.end())
        return nullptr;
    return it->second;
}

bool DominatorTree::dominates(const BasicBlock &a, const BasicBlock &b) const
{
    const BasicBlock *cur = &b;
    while (cur)
    {
        if (cur == &a)
            return true;
        auto it = idoms.find(cur);
        if (it == idoms.end() || it->second == cur)
            break;
        cur = it->second;
    }
    return false;
}

} // namespace il::analysis
