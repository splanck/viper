// File: src/il/transform/SimplifyCFG/BlockMerging.cpp
// License: MIT (see LICENSE for details).
// Purpose: Implements block merging transformations for SimplifyCFG.
// Key invariants: Ensures parameter substitution and terminator rewrites stay consistent.
// Ownership/Lifetime: Mutates predecessor blocks and erases merged successors in place.
// Links: docs/codemap.md

#include "il/transform/SimplifyCFG/BlockMerging.hpp"

#include "il/transform/SimplifyCFG/Utils.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::transform::simplify_cfg
{
namespace
{

bool mergeSinglePred(SimplifyCFG::SimplifyCFGPassContext &ctx, il::core::BasicBlock &block)
{
    il::core::Function &F = ctx.function;

    auto blockIt = std::find_if(
        F.blocks.begin(), F.blocks.end(),
        [&](il::core::BasicBlock &candidate) { return &candidate == &block; });
    if (blockIt == F.blocks.end())
        return false;

    if (ctx.isEHSensitive(block))
        return false;

    il::core::BasicBlock *predBlock = nullptr;
    il::core::Instr *predTerm = nullptr;
    size_t predecessorEdges = 0;

    for (auto &candidate : F.blocks)
    {
        if (&candidate == &block)
            continue;

        il::core::Instr *term = findTerminator(candidate);
        if (!term)
            continue;

        for (size_t idx = 0; idx < term->labels.size(); ++idx)
        {
            if (term->labels[idx] != block.label)
                continue;

            ++predecessorEdges;
            if (predecessorEdges == 1)
            {
                predBlock = &candidate;
                predTerm = term;
            }
        }
    }

    if (predecessorEdges != 1)
        return false;

    if (!predBlock || !predTerm)
        return false;

    if (predTerm->op != il::core::Opcode::Br)
        return false;

    if (predTerm->labels.size() != 1)
        return false;

    if (predTerm->labels.front() != block.label)
        return false;

    il::core::Instr *blockTerm = findTerminator(block);
    if (!blockTerm)
        return false;

    std::vector<il::core::Value> incomingArgs;
    if (!predTerm->brArgs.empty())
    {
        if (predTerm->brArgs.size() != 1)
            return false;
        incomingArgs = predTerm->brArgs.front();
    }

    if (block.params.size() != incomingArgs.size())
        return false;

    std::unordered_map<unsigned, il::core::Value> substitution;
    substitution.reserve(block.params.size());

    for (size_t idx = 0; idx < block.params.size(); ++idx)
        substitution.emplace(block.params[idx].id, incomingArgs[idx]);

    if (!substitution.empty())
    {
        for (auto &instr : block.instructions)
        {
            for (auto &operand : instr.operands)
                operand = substituteValue(operand, substitution);

            for (auto &argList : instr.brArgs)
            {
                for (auto &value : argList)
                    value = substituteValue(value, substitution);
            }
        }
    }

    auto &predInstrs = predBlock->instructions;
    auto predTermIt = std::find_if(
        predInstrs.begin(), predInstrs.end(),
        [&](il::core::Instr &instr) { return &instr == predTerm; });
    if (predTermIt == predInstrs.end())
        return false;

    auto &blockInstrs = block.instructions;
    auto blockTermIt = std::find_if(
        blockInstrs.begin(), blockInstrs.end(),
        [&](il::core::Instr &instr) { return &instr == blockTerm; });
    if (blockTermIt == blockInstrs.end())
        return false;

    std::vector<il::core::Instr> movedInstrs;
    movedInstrs.reserve(blockInstrs.size() > 0 ? blockInstrs.size() - 1 : 0);
    for (auto it = blockInstrs.begin(); it != blockInstrs.end(); ++it)
    {
        if (it == blockTermIt)
            continue;
        movedInstrs.push_back(std::move(*it));
    }

    il::core::Instr newTerm = std::move(*blockTermIt);
    for (auto &label : newTerm.labels)
    {
        if (label == block.label)
            label = predBlock->label;
    }

    predInstrs.erase(predTermIt);

    for (auto &instr : movedInstrs)
        predInstrs.push_back(std::move(instr));

    predInstrs.push_back(std::move(newTerm));
    predBlock->terminated = true;

    size_t blockIndex = static_cast<size_t>(std::distance(F.blocks.begin(), blockIt));
    F.blocks.erase(F.blocks.begin() + static_cast<std::ptrdiff_t>(blockIndex));

    return true;
}

} // namespace

bool mergeSinglePredBlocks(SimplifyCFG::SimplifyCFGPassContext &ctx)
{
    il::core::Function &F = ctx.function;
    bool changed = false;

    size_t blockIndex = 0;
    while (blockIndex < F.blocks.size())
    {
        const bool debugEnabled = ctx.isDebugLoggingEnabled();
        std::string mergedLabel;
        if (debugEnabled)
            mergedLabel = F.blocks[blockIndex].label;

        if (mergeSinglePred(ctx, F.blocks[blockIndex]))
        {
            changed = true;
            ++ctx.stats.blocksMerged;
            if (debugEnabled)
            {
                std::string message = "merged block '" + mergedLabel + "' into its predecessor";
                ctx.logDebug(message);
            }
            continue;
        }
        ++blockIndex;
    }

    return changed;
}

} // namespace il::transform::simplify_cfg

