// File: lib/Analysis/CFG.cpp
// Purpose: Implements minimal CFG utilities for IL blocks and functions.
// Key invariants: Results are computed on demand; no caches or global graphs.
// Ownership/Lifetime: Uses IL objects owned by the caller.
// Links: docs/dev/analysis.md

#include "Analysis/CFG.h"
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

void setModule(const il::core::Module &M)
{
    gModule = &M;
}

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

std::vector<il::core::Block *> reversePostOrder(il::core::Function &F)
{
    auto po = postOrder(F);
    std::reverse(po.begin(), po.end());
    return po;
}

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

bool isAcyclic(il::core::Function &F)
{
    if (F.blocks.empty())
        return true;
    auto order = topoOrder(F);
    return order.size() == F.blocks.size();
}

} // namespace viper::analysis
