//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/SimplifyCFG/ReachabilityCleanup.cpp
// Purpose: Perform reachability walks and erase unreachable blocks for the
//          SimplifyCFG pass.
// Key invariants: Entry block is always treated as reachable; EH-sensitive
//                 blocks (entries/resume targets) are preserved even if the
//                 reachability walk cannot find them; branch metadata (labels
//                 and argument lists) remains consistent after block erasure.
// Ownership/Lifetime: Operates on pass-owned il::core::Function instances and
//                     mutates them in place; no new allocations escape the
//                     function scope.
// Perf/Threading notes: Uses O(N + E) breadth-first search with temporary
//                       queues and bit-vectors; intended for single-threaded
//                       pass execution.
// Links: docs/il-passes.md#simplifycfg, docs/codemap.md#passes
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Reachability-based cleanup helpers for SimplifyCFG.
/// @details Provides breadth-first search routines that compute a reachability
///          mask and helpers that remove unreachable blocks while maintaining
///          branch metadata and respecting exception-handling structure.

#include "il/transform/SimplifyCFG/ReachabilityCleanup.hpp"

#include "il/transform/SimplifyCFG/Utils.hpp"

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::transform::simplify_cfg
{
namespace
{

/// @brief Compute the set of blocks reachable from the entry block.
///
/// @details The helper performs a breadth-first traversal seeded from block
///          index zero (the canonical entry).  For every visited block it:
///          1. Locates the terminator with @ref findTerminator.
///          2. Enumerates successor labels (for conditional/switch/resume).
///          3. Resolves each label to a block index using
///             @ref lookupBlockIndex and enqueues unseen successors via
///             @ref enqueueSuccessor.
///          Blocks that lack terminators are ignored, matching the verifier's
///          assumption that such blocks implicitly fall off the function.
///          The resulting bit vector contains a @c true entry for every block
///          proven reachable.
///
/// @param function Function whose blocks should be analysed.
/// @return Bit vector with bits set for reachable block indices.
BitVector markReachable(il::core::Function &function)
{
    BitVector reachable(function.blocks.size(), false);
    if (function.blocks.empty())
        return reachable;

    std::unordered_map<std::string, size_t> labelToIndex;
    labelToIndex.reserve(function.blocks.size());
    for (size_t idx = 0; idx < function.blocks.size(); ++idx)
        labelToIndex.emplace(function.blocks[idx].label, idx);

    std::deque<size_t> worklist;
    reachable.set(0);
    worklist.push_back(0);

    while (!worklist.empty())
    {
        size_t index = worklist.front();
        worklist.pop_front();

        const il::core::BasicBlock &block = function.blocks[index];
        const il::core::Instr *terminator = findTerminator(block);
        if (!terminator)
            continue;

        auto addLabel = [&](const std::string &label)
        { enqueueSuccessor(reachable, worklist, lookupBlockIndex(labelToIndex, label)); };

        switch (terminator->op)
        {
            case il::core::Opcode::Br:
                if (!terminator->labels.empty())
                    addLabel(terminator->labels.front());
                break;
            case il::core::Opcode::CBr:
            case il::core::Opcode::SwitchI32:
                for (const auto &label : terminator->labels)
                    addLabel(label);
                break;
            case il::core::Opcode::ResumeLabel:
                if (!terminator->labels.empty())
                    addLabel(terminator->labels.front());
                break;
            default:
                break;
        }
    }

    return reachable;
}

} // namespace

/// @brief Remove blocks that are not reachable according to @ref markReachable.
///
/// @details The cleanup routine executes in three phases:
///          1. Build a reachability mask with @ref markReachable and collect
///             indices of blocks that were never visited (excluding the entry).
///          2. Iterate those indices in reverse so erasing blocks does not
///             invalidate still-pending indices.  Each candidate is skipped when
///             @ref SimplifyCFG::SimplifyCFGPassContext::isEHSensitive reports
///             it participates in exception handling.
///          3. For blocks that can be removed, walk every instruction in the
///             function, erase branch labels/arguments that reference the doomed
///             label, and finally erase the block from the function vector.
///          When any block is removed the routine bumps the pass statistics and
///          emits optional debug logging describing the number of pruned blocks.
///
/// @param ctx Pass context providing function, EH sensitivity checks, and stats.
/// @return True when any block was removed.
bool removeUnreachableBlocks(SimplifyCFG::SimplifyCFGPassContext &ctx)
{
    il::core::Function &F = ctx.function;
    BitVector reachable = markReachable(F);

    std::vector<size_t> unreachableBlocks;
    unreachableBlocks.reserve(F.blocks.size());
    for (size_t index = 1; index < F.blocks.size(); ++index)
    {
        if (!reachable.test(index))
            unreachableBlocks.push_back(index);
    }

    size_t removedBlocks = 0;

    for (auto it = unreachableBlocks.rbegin(); it != unreachableBlocks.rend(); ++it)
    {
        const size_t blockIndex = *it;
        if (blockIndex >= F.blocks.size())
            continue;

        il::core::BasicBlock &candidate = F.blocks[blockIndex];
        if (ctx.isEHSensitive(candidate))
            continue;

        const std::string label = candidate.label;

        for (auto &block : F.blocks)
        {
            for (auto &instr : block.instructions)
            {
                if (instr.labels.empty())
                    continue;

                for (size_t idx = 0; idx < instr.labels.size();)
                {
                    if (instr.labels[idx] == label)
                    {
                        instr.labels.erase(instr.labels.begin() + idx);
                        if (idx < instr.brArgs.size())
                            instr.brArgs.erase(instr.brArgs.begin() + idx);
                    }
                    else
                    {
                        ++idx;
                    }
                }
            }
        }

        F.blocks.erase(F.blocks.begin() + static_cast<std::ptrdiff_t>(blockIndex));
        ++removedBlocks;
    }

    if (removedBlocks > 0)
    {
        ctx.stats.unreachableRemoved += removedBlocks;
        if (ctx.isDebugLoggingEnabled())
        {
            std::string message = "erased " + std::to_string(removedBlocks) + " unreachable block" +
                                  (removedBlocks == 1 ? "" : "s");
            ctx.logDebug(message);
        }
        return true;
    }

    return false;
}

} // namespace il::transform::simplify_cfg
