// File: src/il/transform/SimplifyCFG/ReachabilityCleanup.cpp
// License: MIT (see LICENSE for details).
// Purpose: Implements reachability-based cleanup for SimplifyCFG.
// Key invariants: Conservatively removes only unreachable and EH-insensitive blocks.
// Ownership/Lifetime: Updates successor lists and prunes blocks in place.
// Links: docs/codemap.md

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

        auto addLabel = [&](const std::string &label) {
            enqueueSuccessor(reachable, worklist, lookupBlockIndex(labelToIndex, label));
        };

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
            std::string message =
                "erased " + std::to_string(removedBlocks) + " unreachable block" +
                (removedBlocks == 1 ? "" : "s");
            ctx.logDebug(message);
        }
        return true;
    }

    return false;
}

} // namespace il::transform::simplify_cfg

