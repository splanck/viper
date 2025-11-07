//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/SimplifyCFG/BlockMerging.cpp
// Purpose: Merge trivial single-predecessor blocks to reduce CFG complexity in
//          the SimplifyCFG pass.
// Key invariants: Blocks are merged only when there is exactly one incoming
//                 edge, branch argument arity matches block parameters, and
//                 EH-sensitive regions remain untouched.  SSA uses inside the
//                 merged block are rewritten to reference the incoming values.
// Ownership/Lifetime: Mutates the pass-owned il::core::Function in place; no
//                     persistent allocations escape the routine.
// Perf/Threading notes: Uses linear scans over the block list and temporary
//                       vectors for instruction splicing; intended for
//                       single-threaded execution.
// Links: docs/il-passes.md#simplifycfg, docs/codemap.md#passes
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
/// @details The merger proceeds cautiously to preserve SSA and EH invariants:
///          1. Locate @p block inside the function and bail out if the pass
///             marked it EH-sensitive.
///          2. Scan all other blocks to count incoming edges, remembering the
///             unique predecessor and its terminator when exactly one edge is
///             found.
///          3. Require the predecessor terminator to be an unconditional branch
///             with a single label equal to @p block's label so the control-flow
///             structure remains valid after splicing.
///          4. Collect branch arguments from the predecessor and build a
///             substitution map that replaces @p block's parameters with the
///             incoming SSA values.  Every instruction and branch-argument list
///             in the block is rewritten using @ref substituteValue.
///          5. Move all non-terminator instructions from @p block into the
///             predecessor, replace the predecessor's terminator with the merged
///             block's terminator (after fixing self-references), and finally
///             erase @p block from the function.
///          A @c true return indicates the merge succeeded and the caller should
///          treat block indices as potentially invalidated.
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
/// @details Performs a forward walk over the function's block vector.  For each
///          block the helper:
///          - Captures the label (when debug logging is enabled) before any
///            structural changes occur.
///          - Invokes @ref mergeSinglePred to attempt the merge.
///          - When a merge succeeds, increments statistics, emits optional debug
///            output, and repeats the loop without advancing the index because
///            the current position now refers to the successor of the removed
///            block.
///          - Otherwise increments the index and continues scanning.
///          The function returns @c true if at least one merge was performed.
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
