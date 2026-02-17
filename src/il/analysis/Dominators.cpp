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
#include "il/core/Function.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <unordered_set>

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

            auto existingIt = DT.idom.find(b);
            if (existingIt == DT.idom.end())
            {
                DT.idom.emplace(b, newIdom);
                changed = true;
            }
            else if (existingIt->second != newIdom)
            {
                existingIt->second = newIdom;
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

/// @brief Return the immediate post-dominator of block @p B.
il::core::Block *PostDomTree::immediatePostDominator(il::core::Block *B) const
{
    auto it = ipostdom.find(B);
    return it == ipostdom.end() ? nullptr : it->second;
}

/// @brief Check whether block @p A post-dominates block @p B.
/// @details Walks up the post-dominator chain from @p B until reaching the
///          virtual exit (nullptr) or finding @p A.
bool PostDomTree::postDominates(il::core::Block *A, il::core::Block *B) const
{
    if (!A || !B)
        return false;
    if (A == B)
        return true;
    while (B)
    {
        auto it = ipostdom.find(B);
        if (it == ipostdom.end())
            return false;
        B = it->second; // nullptr means we've reached the virtual exit
        if (B == A)
            return true;
    }
    return false;
}

/// @brief Compute post-dominator tree for function @p F.
///
/// @details Applies the Cooper–Harvey–Kennedy iterative algorithm on the
/// reversed CFG.  Exit blocks (no CFG successors) are initialised with
/// @c ipostdom = nullptr, representing the virtual exit node.  All other
/// blocks are processed in reverse-post-order of the reversed CFG, which
/// is obtained by reversing the post-order DFS from the exit blocks.
PostDomTree computePostDominatorTree(const CFGContext &ctx, il::core::Function &F)
{
    PostDomTree PDT;
    if (F.blocks.empty())
        return PDT;

    // -------------------------------------------------------------------------
    // Step 1: Compute post-order of the reversed CFG.
    //
    // A DFS that starts at exit blocks (no successors) and follows predecessors
    // of the original CFG is equivalent to a DFS on the reversed CFG starting
    // from the virtual exit.  Recording blocks in completion order yields the
    // post-order of the reversed CFG; reversing it gives the RPO we need for
    // the CHK iteration.
    // -------------------------------------------------------------------------
    std::vector<il::core::Block *> po_rev;
    po_rev.reserve(F.blocks.size());
    std::unordered_set<il::core::Block *> visited;
    visited.reserve(F.blocks.size());

    std::function<void(il::core::Block *)> dfs = [&](il::core::Block *b)
    {
        visited.insert(b);
        for (auto *pred : predecessors(ctx, *b))
        {
            if (!visited.count(pred))
                dfs(pred);
        }
        po_rev.push_back(b);
    };

    // Start the DFS from all exit blocks (successors of the virtual exit).
    for (auto &bb : F.blocks)
    {
        if (successors(ctx, bb).empty() && !visited.count(&bb))
            dfs(&bb);
    }
    // Handle blocks not reachable from any exit (e.g., infinite-loop bodies).
    for (auto &bb : F.blocks)
    {
        if (!visited.count(&bb))
            dfs(&bb);
    }

    // RPO of reversed CFG: reverse the post-order.
    std::vector<il::core::Block *> rpo_rev(po_rev.rbegin(), po_rev.rend());

    // -------------------------------------------------------------------------
    // Step 2: Assign RPO indices.
    //
    // The virtual exit node is conceptually at index 0 (the "entry" of the
    // reversed CFG).  Real block indices start at 1 so that nullptr (virtual
    // exit) naturally has the smallest index and the CHK intersection converges
    // toward it correctly.
    // -------------------------------------------------------------------------
    std::unordered_map<il::core::Block *, std::size_t> index;
    index.reserve(F.blocks.size() + 1);
    // nullptr (virtual exit) = index 0
    for (std::size_t i = 0; i < rpo_rev.size(); ++i)
        index[rpo_rev[i]] = i + 1;

    auto getIdx = [&](il::core::Block *b) -> std::size_t
    {
        if (!b)
            return 0; // virtual exit
        auto it = index.find(b);
        return it != index.end() ? it->second : std::numeric_limits<std::size_t>::max();
    };

    // -------------------------------------------------------------------------
    // Step 3: Initialise exit blocks.
    //
    // Exit blocks' immediate post-dominator is the virtual exit (nullptr).
    // -------------------------------------------------------------------------
    for (auto &bb : F.blocks)
    {
        if (successors(ctx, bb).empty())
            PDT.ipostdom[&bb] = nullptr;
    }

    // -------------------------------------------------------------------------
    // Step 4: Iterative CHK algorithm on the reversed CFG.
    //
    // For each block in RPO of the reversed CFG, compute the intersection of
    // its successors' immediate post-dominators (successors in the original CFG
    // = predecessors in the reversed CFG).
    // -------------------------------------------------------------------------
    auto intersect = [&](il::core::Block *b1, il::core::Block *b2) -> il::core::Block *
    {
        while (b1 != b2)
        {
            while (getIdx(b1) > getIdx(b2))
            {
                auto it = PDT.ipostdom.find(b1);
                if (it == PDT.ipostdom.end())
                    break;
                b1 = it->second;
            }
            while (getIdx(b2) > getIdx(b1))
            {
                auto it = PDT.ipostdom.find(b2);
                if (it == PDT.ipostdom.end())
                    break;
                b2 = it->second;
            }
        }
        return b1;
    };

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (auto *b : rpo_rev)
        {
            // Exit blocks are already initialised and never change.
            if (successors(ctx, *b).empty())
                continue;

            auto succs = successors(ctx, *b);

            // Find the first already-processed successor as the initial candidate.
            il::core::Block *newIdom = nullptr;
            for (auto *s : succs)
            {
                if (PDT.ipostdom.count(s))
                {
                    newIdom = s;
                    break;
                }
            }
            if (!newIdom)
                continue; // No processed successor yet; defer.

            // Intersect all processed successors.
            for (auto *s : succs)
            {
                if (s == newIdom || !PDT.ipostdom.count(s))
                    continue;
                newIdom = intersect(s, newIdom);
            }

            auto existingIt = PDT.ipostdom.find(b);
            if (existingIt == PDT.ipostdom.end())
            {
                PDT.ipostdom.emplace(b, newIdom);
                changed = true;
            }
            else if (existingIt->second != newIdom)
            {
                existingIt->second = newIdom;
                changed = true;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Step 5: Build child lists.
    // -------------------------------------------------------------------------
    for (auto &[blk, ipd] : PDT.ipostdom)
    {
        if (ipd)
            PDT.children[ipd].push_back(blk);
    }

    return PDT;
}

} // namespace viper::analysis
