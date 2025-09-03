// File: lib/Analysis/CFG.cpp
// Purpose: Implements control-flow graph utilities for IL blocks.
// Key invariants: Uses no caching; recomputes on each call.
// Ownership/Lifetime: Borrowed IL structures remain owned by callers.
// Links: docs/dev/analysis.md

#include "Analysis/CFG.h"

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <algorithm>

namespace viper::analysis
{

std::vector<il::core::BasicBlock *> successors(const il::core::Function &F,
                                               const il::core::BasicBlock &B)
{
    std::vector<il::core::BasicBlock *> succs;
    if (B.instructions.empty())
        return succs;

    const il::core::Instr &term = B.instructions.back();
    using il::core::Opcode;
    if (term.op == Opcode::Br || term.op == Opcode::CBr)
    {
        for (const auto &label : term.labels)
        {
            auto it =
                std::find_if(F.blocks.begin(),
                             F.blocks.end(),
                             [&](const il::core::BasicBlock &blk) { return blk.label == label; });
            if (it != F.blocks.end())
                succs.push_back(const_cast<il::core::BasicBlock *>(&*it));
        }
    }
    return succs;
}

std::vector<il::core::BasicBlock *> predecessors(const il::core::Function &F,
                                                 const il::core::BasicBlock &B)
{
    std::vector<il::core::BasicBlock *> preds;
    for (const auto &blk : F.blocks)
    {
        if (blk.instructions.empty())
            continue;
        const il::core::Instr &term = blk.instructions.back();
        using il::core::Opcode;
        if (term.op != Opcode::Br && term.op != Opcode::CBr)
            continue;
        if (std::find(term.labels.begin(), term.labels.end(), B.label) != term.labels.end())
            preds.push_back(const_cast<il::core::BasicBlock *>(&blk));
    }
    return preds;
}

} // namespace viper::analysis
