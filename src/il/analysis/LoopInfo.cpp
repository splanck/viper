//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/analysis/LoopInfo.cpp
// Purpose: Implement natural loop discovery using CFG traversal and dominance.
// Links: docs/architecture.md#analysis
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements loop detection utilities built on top of CFG and dominator
/// analyses. The routine identifies backedges, reconstructs natural loops by
/// walking predecessors, and assembles a loop forest that reflects nesting
/// relationships.

#include "il/analysis/LoopInfo.hpp"

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::analysis
{
namespace
{
struct LoopRecord
{
    std::unique_ptr<Loop> LoopNode;
    std::unordered_set<il::core::BasicBlock *> Blocks;
};

LoopRecord &getOrCreateRecord(il::core::BasicBlock *header,
                              std::vector<std::unique_ptr<LoopRecord>> &records,
                              std::unordered_map<il::core::BasicBlock *, LoopRecord *> &map)
{
    auto it = map.find(header);
    if (it != map.end())
        return *it->second;

    auto record = std::make_unique<LoopRecord>();
    record->LoopNode = std::make_unique<Loop>();
    record->LoopNode->Header = header;
    record->Blocks.insert(header);
    record->LoopNode->Blocks.push_back(header);

    records.push_back(std::move(record));
    LoopRecord *ptr = records.back().get();
    map[header] = ptr;
    return *ptr;
}

bool containsBlock(const LoopRecord &record, il::core::BasicBlock *block)
{
    return record.Blocks.count(block) > 0;
}

bool isSubset(const LoopRecord &needle, const LoopRecord &haystack)
{
    if (!containsBlock(haystack, needle.LoopNode->Header))
        return false;
    for (auto *block : needle.Blocks)
    {
        if (!containsBlock(haystack, block))
            return false;
    }
    return true;
}

const Loop *findInnermost(const Loop *loop, const il::core::BasicBlock *block)
{
    if (!loop)
        return nullptr;
    auto inCurrent = std::find(loop->Blocks.begin(), loop->Blocks.end(), block);
    if (inCurrent == loop->Blocks.end())
        return nullptr;
    for (const auto &child : loop->Children)
    {
        if (const Loop *nested = findInnermost(child.get(), block))
            return nested;
    }
    return loop;
}

} // namespace

LoopInfo LoopInfo::compute(il::core::Module &module, il::core::Function &function, const DomTree &dom)
{
    LoopInfo info;

    CFGContext ctx(module);

    std::vector<std::unique_ptr<LoopRecord>> records;
    std::unordered_map<il::core::BasicBlock *, LoopRecord *> headerToRecord;

    for (auto &bb : function.blocks)
    {
        il::core::BasicBlock *block = &bb;
        auto succs = successors(ctx, bb);
        for (auto *succ : succs)
        {
            if (!succ)
                continue;
            if (!dom.dominates(succ, block))
                continue;

            LoopRecord &record = getOrCreateRecord(succ, records, headerToRecord);

            if (std::find(record.LoopNode->Latches.begin(), record.LoopNode->Latches.end(), block) ==
                record.LoopNode->Latches.end())
            {
                record.LoopNode->Latches.push_back(block);
            }

            std::vector<il::core::BasicBlock *> worklist;
            worklist.push_back(block);
            while (!worklist.empty())
            {
                il::core::BasicBlock *current = worklist.back();
                worklist.pop_back();

                if (!record.Blocks.insert(current).second)
                    continue;
                record.LoopNode->Blocks.push_back(current);

                auto preds = predecessors(ctx, *current);
                for (auto *pred : preds)
                {
                    if (pred == record.LoopNode->Header)
                    {
                        if (record.Blocks.insert(pred).second)
                            record.LoopNode->Blocks.push_back(pred);
                        continue;
                    }
                    if (!containsBlock(record, pred))
                        worklist.push_back(pred);
                }
            }
        }
    }

    for (auto &recordPtr : records)
    {
        Loop &loop = *recordPtr->LoopNode;
        std::unordered_set<il::core::BasicBlock *> exitSet;
        for (auto *block : loop.Blocks)
        {
            auto succs = successors(ctx, *block);
            for (auto *succ : succs)
            {
                if (recordPtr->Blocks.count(succ) == 0 && exitSet.insert(succ).second)
                    loop.Exits.push_back(succ);
            }
        }
    }

    for (auto &recordPtr : records)
    {
        LoopRecord *current = recordPtr.get();
        Loop *parent = nullptr;
        std::size_t parentSize = std::numeric_limits<std::size_t>::max();
        for (auto &candidatePtr : records)
        {
            if (candidatePtr.get() == current)
                continue;
            if (!isSubset(*current, *candidatePtr))
                continue;
            if (candidatePtr->Blocks.size() < parentSize)
            {
                parent = candidatePtr->LoopNode.get();
                parentSize = candidatePtr->Blocks.size();
            }
        }
        current->LoopNode->Parent = parent;
    }

    for (auto &recordPtr : records)
    {
        Loop *parent = recordPtr->LoopNode->Parent;
        if (parent)
            parent->Children.push_back(std::move(recordPtr->LoopNode));
    }

    for (auto &recordPtr : records)
    {
        if (recordPtr->LoopNode)
            info.TopLevel.push_back(std::move(recordPtr->LoopNode));
    }

    return info;
}

const Loop *LoopInfo::getLoopFor(const il::core::BasicBlock *block) const noexcept
{
    if (!block)
        return nullptr;
    for (const auto &loop : TopLevel)
    {
        if (const Loop *found = findInnermost(loop.get(), block))
            return found;
    }
    return nullptr;
}

} // namespace viper::analysis
