//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/analysis/Dominators.cpp
// Purpose: Implement the Cooper–Harvey–Kennedy dominator tree algorithm and
//          expose dominance queries for IL CFGs.
// Links: docs/architecture.md#analysis
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements dominator tree construction and query helpers.
/// @details Provides an out-of-line home for the algorithm so the header can
///          remain lightweight while documenting how dominance intersections are
///          computed and cached.

#include "il/analysis/Dominators.hpp"
#include "il/analysis/CFG.hpp"
#include <cstddef>

namespace viper::analysis
{

/// @brief Return the immediate dominator for a block.
/// @details Performs a map lookup against the cached immediate dominator table.
///          Entry blocks are stored with null dominators and therefore produce
///          `nullptr` results.  The tree must have been previously computed via
///          @ref computeDominatorTree.
/// @param B Block whose immediate dominator is sought.
/// @return Immediate dominator of @p B or `nullptr` if @p B is the entry block.
/// @invariant The dominator tree has been previously computed for the
/// containing function.
il::core::Block *DomTree::immediateDominator(il::core::Block *B) const
{
    auto it = idom.find(B);
    return it == idom.end() ? nullptr : it->second;
}

/// @brief Check whether one block dominates another.
/// @details Walks up the dominator chain from @p B until reaching the entry or
///          encountering @p A.  Missing dominator entries terminate the search
///          early, signalling that the tree was not fully populated for the
///          block (such as unreachable regions).
/// @param A Potential dominator.
/// @param B Block being tested for domination.
/// @return `true` if @p A dominates @p B, otherwise `false`.
/// @invariant Both blocks belong to the same function and the dominator tree
/// is fully built.
bool DomTree::dominates(il::core::Block *A, il::core::Block *B) const
{
    if (!A || !B)
        return false;
    if (A == B)
        return true;
    while (B)
    {
        auto it = idom.find(B);
        if (it == idom.end())
            return false;
        B = it->second;
        if (B == A)
            return true;
    }
    return false;
}

/// @brief Construct the dominator tree for a function.
/// @details Implements the Cooper–Harvey–Kennedy algorithm to derive immediate
///          dominators for every reachable block.  Builds a reverse-postorder
///          traversal, iteratively refines the immediate dominator map using the
///          standard intersection routine, and finally populates child lists for
///          convenience.
/// @param ctx CFG context used to access traversal helpers.
/// @param F Function whose dominator relationships are computed.
/// @return A fully populated dominator tree with parent and child links.
/// @invariant The function must have a valid control-flow graph with a
/// single entry block.
DomTree computeDominatorTree(const CFGContext &ctx, il::core::Function &F)
{
    DomTree DT;
    auto rpo = reversePostOrder(ctx, F);
    if (rpo.empty())
        return DT;

    std::unordered_map<il::core::Block *, std::size_t> index;
    for (std::size_t i = 0; i < rpo.size(); ++i)
        index[rpo[i]] = i;

    il::core::Block *entry = rpo.front();
    DT.idom[entry] = nullptr;

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (std::size_t i = 1; i < rpo.size(); ++i)
        {
            il::core::Block *b = rpo[i];
            auto preds = predecessors(ctx, *b);

            il::core::Block *newIdom = nullptr;
            for (auto *p : preds)
            {
                if (DT.idom.contains(p))
                {
                    newIdom = p;
                    break;
                }
            }
            if (!newIdom)
                continue;

            // Intersect two dominance paths by advancing along the dominator
            // chain using block visit indexes until the nearest common
            // ancestor is located.
            auto intersect = [&](il::core::Block *b1, il::core::Block *b2)
            {
                while (b1 != b2)
                {
                    while (index[b1] > index[b2])
                        b1 = DT.idom[b1];
                    while (index[b2] > index[b1])
                        b2 = DT.idom[b2];
                }
                return b1;
            };

            for (auto *p : preds)
            {
                if (p == newIdom || !DT.idom.contains(p))
                    continue;
                newIdom = intersect(p, newIdom);
            }

            if (!DT.idom.contains(b) || DT.idom[b] != newIdom)
            {
                DT.idom[b] = newIdom;
                changed = true;
            }
        }
    }

    for (auto &[blk, id] : DT.idom)
    {
        if (id)
            DT.children[id].push_back(blk);
    }

    return DT;
}

} // namespace viper::analysis
