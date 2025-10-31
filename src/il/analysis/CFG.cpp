//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements control-flow graph utilities for IL functions, including
// successor/predecessor queries and common block orderings.  The helpers cache
// label lookups so repeated analyses can operate without rebuilding indexes.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Control-flow graph helpers for IL analyses.
/// @details Provides cached successor/predecessor lookups plus traversal
///          algorithms such as post-order, reverse post-order, and topological
///          ordering.  The context precomputes per-function indices so
///          subsequent passes can query the CFG without re-scanning blocks,
///          improving both clarity and performance for analyses layered on top.

#include "il/analysis/CFG.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include <algorithm>
#include <cstddef>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>

namespace viper::analysis
{

/// @brief Construct a CFG analysis context for the provided module.
///
/// Builds per-function label lookup tables, pre-populates successor caches for
/// each block's branch targets, and records predecessor lists by inverting the
/// discovered successor edges. The constructor also tracks the parent function
/// for every block encountered so that subsequent queries can resolve
/// relationships efficiently.
/// @param module IL module whose functions and blocks seed the CFG caches.
CFGContext::CFGContext(il::core::Module &module) : module(&module)
{
    for (auto &fn : module.functions)
    {
        auto &labelMap = functionLabelToBlock[&fn];
        for (auto &blk : fn.blocks)
        {
            blockToFunction[&blk] = &fn;
            labelMap.emplace(blk.label, &blk);
            blockSuccessors[&blk];
            blockPredecessors[&blk];
        }
    }

    for (auto &fn : module.functions)
    {
        auto &labelMap = functionLabelToBlock[&fn];
        for (auto &blk : fn.blocks)
        {
            auto &succ = blockSuccessors[&blk];
            if (blk.instructions.empty())
                continue;

            const il::core::Instr &term = blk.instructions.back();
            bool isBranchTerminator = false;
            switch (term.op)
            {
                case il::core::Opcode::Br:
                case il::core::Opcode::CBr:
                case il::core::Opcode::SwitchI32:
                case il::core::Opcode::ResumeLabel:
                    isBranchTerminator = true;
                    break;
                default:
                    break;
            }

            if (!isBranchTerminator)
                continue;

            auto appendLabel = [&](const std::string &label)
            {
                auto it = labelMap.find(label);
                if (it == labelMap.end())
                    return;
                succ.push_back(it->second);
            };

            if (term.op == il::core::Opcode::SwitchI32)
            {
                if (!term.labels.empty())
                {
                    appendLabel(il::core::switchDefaultLabel(term));
                    const std::size_t caseCount = il::core::switchCaseCount(term);
                    for (std::size_t idx = 0; idx < caseCount; ++idx)
                        appendLabel(il::core::switchCaseLabel(term, idx));
                }
            }
            else
            {
                for (const auto &lbl : term.labels)
                    appendLabel(lbl);
            }

            std::unordered_set<il::core::Block *> recorded;
            recorded.reserve(succ.size());
            for (auto *target : succ)
            {
                if (!target || recorded.count(target))
                    continue;
                recorded.insert(target);
                blockPredecessors[target].push_back(&blk);
            }
        }
    }
}

/// @brief Gather successor blocks of @p B.
///
/// @details Looks up @p B in the context's cache and returns the branch targets
///          recorded during construction.  Because the cache only records
///          terminator-imposed edges, the helper runs in constant time and
///          avoids re-scanning instructions.  Missing entries simply yield an
///          empty vector so callers can treat absent edges as "no successors".
/// @param ctx Context providing access to the parent function mapping.
/// @param B Block whose outgoing edges are requested.
/// @return List of successor blocks; empty when the block was not indexed or
///         does not terminate with a branch.
/// @invariant A valid CFGContext describing @p B's parent function is provided.
std::vector<il::core::Block *> successors(const CFGContext &ctx, const il::core::Block &B)
{
    auto it = ctx.blockSuccessors.find(&B);
    if (it == ctx.blockSuccessors.end())
        return {};
    return it->second;
}

/// @brief Gather predecessor blocks of @p B within its parent function.
///
/// @details Returns the cached predecessor list accumulated during context
///          construction.  Each entry represents a block that branches to
///          @p B.  Because duplicates are removed when caching, the result set is
///          unique and suitable for algorithms that assume each predecessor is
///          visited once.
/// @param ctx Context providing block-to-function lookup.
/// @param B Target block whose incoming edges are requested.
/// @return List of predecessor blocks; empty if none are found.
/// @note Blocks with non-branch terminators are ignored.
std::vector<il::core::Block *> predecessors(const CFGContext &ctx, const il::core::Block &B)
{
    auto it = ctx.blockPredecessors.find(&B);
    if (it == ctx.blockPredecessors.end())
        return {};
    return it->second;
}

/// @brief Compute a depth-first post-order traversal of @p F.
///
/// @details Implements an explicit-stack DFS that tracks which successor index
///          is currently being explored.  A block is appended to the output only
///          after all reachable successors have been processed, mirroring the
///          behaviour of a recursive DFS without relying on recursion.  Blocks
///          unreachable from the entry block never enter the stack, so they are
///          naturally excluded from the ordering.
/// @param ctx Context providing successor lookups.
/// @param F Function whose blocks are traversed.
/// @return Blocks in post-order; empty if @p F has no blocks.
/// @note Unreachable blocks are omitted from the result.
std::vector<il::core::Block *> postOrder(const CFGContext &ctx, il::core::Function &F)
{
    std::vector<il::core::Block *> out;
    if (F.blocks.empty())
        return out;

    std::unordered_set<il::core::Block *> visited;

    struct Frame
    {
        il::core::Block *block;
        std::size_t idx;
        std::vector<il::core::Block *> succ;
    };

    std::vector<Frame> stack;

    il::core::Block *entry = &F.blocks[0];
    stack.push_back({entry, 0, successors(ctx, *entry)});
    visited.insert(entry);

    while (!stack.empty())
    {
        Frame &f = stack.back();
        if (f.idx < f.succ.size())
        {
            il::core::Block *next = f.succ[f.idx++];
            if (!visited.count(next))
            {
                visited.insert(next);
                stack.push_back({next, 0, successors(ctx, *next)});
            }
        }
        else
        {
            out.push_back(f.block);
            stack.pop_back();
        }
    }
    return out;
}

/// @brief Compute reverse post-order (RPO) traversal of @p F.
///
/// @details Calls @ref postOrder and reverses the resulting vector so the entry
///          block appears first.  RPO is a common visitation order for forward
///          data-flow analyses, and computing it by reversing post-order keeps
///          the implementation compact while inheriting the reachability filter
///          from the DFS.
/// @param ctx Context providing successor lookups.
/// @param F Function whose blocks are traversed.
/// @return Blocks in reverse post-order; empty if @p F has no blocks.
std::vector<il::core::Block *> reversePostOrder(const CFGContext &ctx, il::core::Function &F)
{
    auto po = postOrder(ctx, F);
    std::reverse(po.begin(), po.end());
    return po;
}

/// @brief Compute a topological ordering of blocks in @p F.
///
/// @details Executes Kahn's algorithm: initialise in-degree counts for each
///          block, seed a queue with zero in-degree blocks, and repeatedly
///          remove blocks while decrementing the in-degree of successors.  If a
///          cycle prevents the queue from consuming every block the function
///          returns an empty vector to signal the failure.
/// @param ctx Context providing predecessor/successor lookups.
/// @param F Function whose blocks are ordered.
/// @return Blocks in topological order or an empty list if @p F is empty
/// or cyclic.
/// @invariant @p ctx describes the module containing @p F.
std::vector<il::core::Block *> topoOrder(const CFGContext &ctx, il::core::Function &F)
{
    std::vector<il::core::Block *> out;
    if (F.blocks.empty())
        return out;

    std::unordered_map<il::core::Block *, std::size_t> indegree;
    indegree.reserve(F.blocks.size());
    for (auto &blk : F.blocks)
        indegree[&blk] = predecessors(ctx, blk).size();

    std::queue<il::core::Block *> q;
    for (auto &blk : F.blocks)
        if (indegree[&blk] == 0)
            q.push(&blk);

    while (!q.empty())
    {
        auto *b = q.front();
        q.pop();
        out.push_back(b);
        for (auto *succ : successors(ctx, *b))
        {
            auto it = indegree.find(succ);
            if (it == indegree.end())
                continue;
            if (--(it->second) == 0)
                q.push(succ);
        }
    }

    if (out.size() != F.blocks.size())
        return {};
    return out;
}

/// @brief Determine whether the CFG of @p F contains a cycle.
///
/// @details Runs @ref topoOrder and compares the number of blocks produced
///          against the function's block count.  Because @ref topoOrder returns
///          an empty vector for cyclic graphs, equality indicates an acyclic CFG
///          while a mismatch identifies at least one cycle.
/// @param ctx Context providing successor and predecessor lookups.
/// @param F Function whose CFG is inspected.
/// @return `true` if @p F has no cycles or no blocks; otherwise `false`.
bool isAcyclic(const CFGContext &ctx, il::core::Function &F)
{
    if (F.blocks.empty())
        return true;
    auto order = topoOrder(ctx, F);
    return order.size() == F.blocks.size();
}

} // namespace viper::analysis
