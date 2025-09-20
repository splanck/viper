// File: src/il/analysis/CFG.cpp
// Purpose: Implements minimal CFG utilities for IL blocks and functions.
// Key invariants: Results are computed on demand; no caches or global graphs.
// Ownership/Lifetime: Uses IL objects owned by the caller.
// Links: docs/dev/analysis.md

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

namespace
{
const il::core::Module *gModule = nullptr;
}

namespace viper::analysis
{

/// @brief Set the module used for subsequent CFG queries.
///
/// Stores a pointer to @p M so that later traversals can resolve block
/// labels to concrete blocks when computing edges.
/// @param M Module providing function and block context for queries.
void setModule(const il::core::Module &M)
{
    gModule = &M;
}

/// @brief Gather successor blocks of @p B.
///
/// Examines the terminator instruction of @p B and returns the blocks
/// targeted by branch or conditional branch operations.
/// @param B Block whose outgoing edges are requested.
/// @return List of successor blocks. The list is empty if the current
/// module is unset, @p B lacks a branch terminator, or the targets cannot
/// be resolved.
/// @invariant setModule() has been invoked prior to calling.
std::vector<il::core::Block *> successors(const il::core::Block &B)
{
    std::vector<il::core::Block *> out;
    if (!gModule || B.instructions.empty())
        return out;

    const il::core::Instr &term = B.instructions.back();
    if (term.op != il::core::Opcode::Br && term.op != il::core::Opcode::CBr)
        return out;

    const il::core::Function *parent = nullptr;
    for (const auto &fn : gModule->functions)
    {
        for (const auto &blk : fn.blocks)
        {
            if (&blk == &B)
            {
                parent = &fn;
                break;
            }
        }
        if (parent)
            break;
    }
    if (!parent)
        return out;

    for (const auto &lbl : term.labels)
    {
        for (auto &blk : parent->blocks)
        {
            if (blk.label == lbl)
            {
                out.push_back(const_cast<il::core::Block *>(&blk));
                break;
            }
        }
    }
    return out;
}

/// @brief Gather predecessor blocks of @p B within function @p F.
///
/// Scans every block in @p F for branch or conditional branch terminators
/// that reference @p B by label.
/// @param F Function containing the blocks to scan.
/// @param B Target block whose incoming edges are requested.
/// @return List of predecessor blocks; empty if none are found.
/// @note Blocks with non-branch terminators are ignored.
std::vector<il::core::Block *> predecessors(const il::core::Function &F, const il::core::Block &B)
{
    std::vector<il::core::Block *> out;
    for (auto &blk : F.blocks)
    {
        if (blk.instructions.empty())
            continue;
        const il::core::Instr &term = blk.instructions.back();
        if (term.op != il::core::Opcode::Br && term.op != il::core::Opcode::CBr)
            continue;
        for (const auto &lbl : term.labels)
        {
            if (lbl == B.label)
            {
                out.push_back(const_cast<il::core::Block *>(&blk));
                break;
            }
        }
    }
    return out;
}

/// @brief Compute a depth-first post-order traversal of @p F.
///
/// Performs an iterative DFS starting from the entry block and records
/// each block after all its successors have been visited. Only blocks
/// reachable from the entry block are included.
/// @param F Function whose blocks are traversed.
/// @return Blocks in post-order; empty if @p F has no blocks.
/// @invariant setModule() has been called so successor edges can be
/// resolved.
/// @note Unreachable blocks are omitted from the result.
std::vector<il::core::Block *> postOrder(il::core::Function &F)
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
    stack.push_back({entry, 0, successors(*entry)});
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
                stack.push_back({next, 0, successors(*next)});
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
/// Generates the post-order sequence and then reverses it so that the
/// entry block appears first. Only blocks reachable from the entry block
/// are present.
/// @param F Function whose blocks are traversed.
/// @return Blocks in reverse post-order; empty if @p F has no blocks.
std::vector<il::core::Block *> reversePostOrder(il::core::Function &F)
{
    auto po = postOrder(F);
    std::reverse(po.begin(), po.end());
    return po;
}

/// @brief Compute a topological ordering of blocks in @p F.
///
/// Uses Kahn's algorithm to order blocks such that all edges go from
/// earlier to later blocks. If the graph contains a cycle, an empty
/// vector is returned.
/// @param F Function whose blocks are ordered.
/// @return Blocks in topological order or an empty list if @p F is empty
/// or cyclic.
/// @invariant setModule() has been invoked so successor edges can be
/// inspected.
std::vector<il::core::Block *> topoOrder(il::core::Function &F)
{
    std::vector<il::core::Block *> out;
    if (F.blocks.empty())
        return out;

    std::unordered_map<il::core::Block *, std::size_t> indegree;
    indegree.reserve(F.blocks.size());
    for (auto &blk : F.blocks)
        indegree[&blk] = predecessors(F, blk).size();

    std::queue<il::core::Block *> q;
    for (auto &blk : F.blocks)
        if (indegree[&blk] == 0)
            q.push(&blk);

    while (!q.empty())
    {
        auto *b = q.front();
        q.pop();
        out.push_back(b);
        for (auto *succ : successors(*b))
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
/// Delegates to topoOrder() and compares the number of blocks returned
/// against the total blocks in @p F.
/// @param F Function whose CFG is inspected.
/// @return `true` if @p F has no cycles or no blocks; otherwise `false`.
/// @invariant setModule() has been invoked so successor edges can be
/// resolved.
bool isAcyclic(il::core::Function &F)
{
    if (F.blocks.empty())
        return true;
    auto order = topoOrder(F);
    return order.size() == F.blocks.size();
}

} // namespace viper::analysis
