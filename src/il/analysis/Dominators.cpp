//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements dominator tree construction using the Cooper–Harvey–Kennedy
// algorithm.  The helper builds the immediate-dominator relation for every
// block in a function, caches the relationship for quick dominance queries, and
// synthesizes child lists so consumers can traverse the dominator tree without
// recomputing fixpoints.  All data structures operate directly on the IL owned
// by the caller; no additional copies of the control-flow graph are created.
//
//===----------------------------------------------------------------------===//

#include "il/analysis/Dominators.hpp"
#include "il/analysis/CFG.hpp"

#include <cstddef>
#include <unordered_map>

namespace viper::analysis
{

/// @brief Retrieve the parent of a block within the dominator tree.
///
/// The lookup is performed against the cached immediate-dominator mapping that
/// `computeDominatorTree()` constructs.  A null pointer is returned when the
/// queried block is the function entry (which has no predecessor in the tree)
/// or when the block was never inserted into the map because the dominator tree
/// has not been fully computed yet.
///
/// @param B Block whose immediate dominator is sought.
/// @return Immediate dominator of @p B, or `nullptr` when no parent exists.
/// @invariant The dominator tree has been previously computed for the
/// containing function.
il::core::Block *DomTree::immediateDominator(il::core::Block *B) const
{
    auto it = idom.find(B);
    return it == idom.end() ? nullptr : it->second;
}

/// @brief Determine whether a candidate block dominates another block.
///
/// The routine climbs the dominator chain starting at @p B and repeatedly
/// follows immediate-dominator links until either the chain reaches @p A or the
/// root of the tree.  Encountering @p A proves dominance; reaching the root
/// without seeing @p A demonstrates that @p A does not dominate @p B.  Null
/// arguments are treated as non-dominating so callers can short-circuit on
/// malformed IR.
///
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

/// @brief Construct the dominator tree for a function using CHK iteration.
///
/// The algorithm first gathers the reverse post-order (RPO) over the function's
/// reachable blocks, assigns each block an RPO index, and then iteratively
/// refines the immediate-dominator relation by intersecting predecessor chains.
/// The fixpoint computation mirrors Cooper–Harvey–Kennedy and typically
/// converges in a small number of passes.  Once the mapping is stable the tree
/// is reified into both parent and child adjacency to support downstream
/// analyses.
///
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
                if (DT.idom.count(p))
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
                if (p == newIdom || !DT.idom.count(p))
                    continue;
                newIdom = intersect(p, newIdom);
            }

            if (!DT.idom.count(b) || DT.idom[b] != newIdom)
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

