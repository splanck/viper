//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the SimplifyCFG block merging routines.  These helpers detect
// blocks with a single predecessor and merge their contents into the predecessor
// once branch arguments are substituted.  The transformation reduces the number
// of basic blocks while keeping control flow and SSA operands consistent.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Block merging utilities for SimplifyCFG.
/// @details Provides the worker that performs the actual merge and the public
///          entry point that walks the function, records statistics, and emits
///          debug diagnostics.

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

/// @brief Merge a block into its sole predecessor when safe.
///
/// @details Searches for a single predecessor edge targeting @p block, verifies
///          that the predecessor terminator is a simple branch, and rewrites all
///          SSA uses in @p block to reference the incoming arguments.  The
///          routine then splices @p block's non-terminator instructions into the
///          predecessor, rewrites the successor labels in the merged terminator,
///          and finally erases the now-redundant block from the function.
///
/// @param ctx   SimplifyCFG context providing EH-sensitivity checks and
///              diagnostic hooks.
/// @param block Candidate block to merge.
/// @returns True when the block was merged into its predecessor.
bool mergeSinglePred(SimplifyCFG::SimplifyCFGPassContext &ctx, il::core::BasicBlock &block)
{
    il::core::Function &F = ctx.function;

    auto blockIt =
        std::find_if(F.blocks.begin(),
                     F.blocks.end(),
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
    auto predTermIt = std::find_if(predInstrs.begin(),
                                   predInstrs.end(),
                                   [&](il::core::Instr &instr) { return &instr == predTerm; });
    if (predTermIt == predInstrs.end())
        return false;

    auto &blockInstrs = block.instructions;
    auto blockTermIt = std::find_if(blockInstrs.begin(),
                                    blockInstrs.end(),
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

/// @brief Merge every eligible single-predecessor block in a function.
///
/// @details Iterates blocks in order, attempting to merge each into its
///          predecessor via @ref mergeSinglePred.  The walk repeats for the next
///          block only when no merge occurred, ensuring indices remain valid as
///          blocks are erased.  When merges succeed the helper updates
///          statistics and emits optional debug output so callers can understand
///          the transformations performed.
///
/// @param ctx SimplifyCFG context owning the function under transformation.
/// @returns True if any block was merged.
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
