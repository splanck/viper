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

} // namespace viper::analysis
