//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lightweight loop detection helpers derived from CFG and dominator
// information.  The analysis records loop membership by label to remain stable
// even when passes insert additional blocks after the summary was computed.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements loop discovery and metadata for IL functions.
/// @details Builds loop headers, bodies, nesting relationships, and exit edges
///          using CFG and dominator information. Labels are stored instead of
///          pointers so results stay valid after block reordering.

#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/utils/Utils.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <unordered_map>

using namespace il::core;

namespace il::transform {

/// @brief Check whether a loop contains the block with @p label.
/// @details Uses a cached hash set populated by @ref Loop::finalize to provide
///          constant-time membership checks without scanning all labels.
/// @param label Basic block label to query.
/// @return True if the block is part of the loop; false otherwise.
bool Loop::contains(std::string_view label) const {
    // Heterogeneous lookup - no temporary std::string allocation
    return members_.find(label) != members_.end();
}

/// @brief Finalize loop membership caches after mutation.
/// @details Rebuilds the internal hash set from @ref blockLabels so membership
///          checks are fast and consistent with the label list.
void Loop::finalize() {
    members_.clear();
    members_.reserve(blockLabels.size());
    for (const auto &label : blockLabels)
        members_.insert(label);
}

/// @brief Find a loop by its header label.
/// @details Performs a linear search over the recorded loops. Loop counts are
///          typically small, so a vector scan keeps the implementation simple.
/// @param headerLabel Label of the loop header block.
/// @return Pointer to the loop metadata, or nullptr if not found.
const Loop *LoopInfo::findLoop(std::string_view headerLabel) const {
    for (const auto &loop : loops_) {
        if (loop.headerLabel == headerLabel)
            return &loop;
    }
    return nullptr;
}

/// @brief Add a loop to the analysis after preparing its membership cache.
/// @details Calls @ref Loop::finalize to populate cached membership before
///          storing the loop metadata.
/// @param loop Loop metadata to add; moved into internal storage.
void LoopInfo::addLoop(Loop loop) {
    loop.finalize();
    loops_.push_back(std::move(loop));
}

/// @brief Look up the parent loop for a nested loop.
/// @details Uses the stored parent header label to find the enclosing loop.
/// @param loop Child loop to query.
/// @return Pointer to the parent loop, or nullptr for top-level loops.
const Loop *LoopInfo::parent(const Loop &loop) const {
    if (loop.parentHeader.empty())
        return nullptr;
    return findLoop(loop.parentHeader);
}

namespace {
/// @brief Collect predecessor blocks for @p block using CFG context data.
/// @details Returns the cached predecessor edge list from the CFG context.
///          Duplicate entries are preserved because IL branch arguments are
///          edge-specific.
/// @param ctx CFG context holding predecessor maps.
/// @param block Block whose predecessors should be returned.
/// @return Reference to predecessor pointers (may be empty).
const std::vector<BasicBlock *> &getPredecessors(const viper::analysis::CFGContext &ctx,
                                                 const BasicBlock &block) {
    return viper::analysis::predecessors(ctx, block);
}

} // namespace

/// @brief Compute loop information for a function.
/// @details Identifies natural loops by locating backedges (predecessors
///          dominated by the header), gathers loop bodies by walking dominated
///          predecessors, and records latch blocks. After discovery, parent/
///          child nesting is computed and exit edges are captured by scanning
///          terminator successors that leave the loop.
/// @param module Module providing shared CFG context.
/// @param function Function to analyze for loop structure.
/// @return Populated LoopInfo describing headers, bodies, nesting, and exits.
LoopInfo computeLoopInfo(Module &module, Function &function) {
    LoopInfo info;

    viper::analysis::CFGContext cfgCtx(module, function);
    viper::analysis::DomTree domTree = viper::analysis::computeDominatorTree(cfgCtx, function);

    // Discover loops (header, body, latches).
    for (auto &block : function.blocks) {
        std::vector<BasicBlock *> latchBlocks;
        for (BasicBlock *pred : getPredecessors(cfgCtx, block)) {
            if (domTree.dominates(&block, pred))
                latchBlocks.push_back(pred);
        }

        if (latchBlocks.empty())
            continue;

        Loop loop;
        loop.headerLabel = block.label;
        loop.blockLabels.push_back(block.label);

        std::deque<BasicBlock *> worklist;
        std::unordered_map<BasicBlock *, bool> visited;
        visited[&block] = true;

        for (BasicBlock *latch : latchBlocks) {
            if (!visited[latch]) {
                worklist.push_back(latch);
                visited[latch] = true;
                loop.blockLabels.push_back(latch->label);
            }
            loop.latchLabels.push_back(latch->label);
        }

        while (!worklist.empty()) {
            BasicBlock *current = worklist.front();
            worklist.pop_front();

            for (BasicBlock *pred : getPredecessors(cfgCtx, *current)) {
                if (!domTree.dominates(&block, pred))
                    continue;
                if (visited[pred])
                    continue;
                visited[pred] = true;
                worklist.push_back(pred);
                loop.blockLabels.push_back(pred->label);
            }
        }

        info.addLoop(std::move(loop));
    }

    // Parent/child nesting (pick smallest containing loop as parent).
    // Pre-cache loop sizes for O(1) lookup instead of O(n) findLoop() calls.
    std::unordered_map<std::string, size_t> loopSizes;
    loopSizes.reserve(info.loops_.size());
    for (const auto &loop : info.loops_) {
        loopSizes[loop.headerLabel] = loop.blockLabels.size();
    }

    auto contains = [](const Loop &loop, std::string_view label) { return loop.contains(label); };
    for (auto &loop : info.loops_) {
        std::optional<std::string> parent;
        size_t parentSize = SIZE_MAX;
        for (const auto &candidate : info.loops_) {
            if (candidate.headerLabel == loop.headerLabel)
                continue;
            if (!contains(candidate, loop.headerLabel))
                continue;
            // Use cached size lookup instead of findLoop()->blockLabels.size()
            if (!parent || candidate.blockLabels.size() < parentSize) {
                parent = candidate.headerLabel;
                parentSize = candidate.blockLabels.size();
            }
        }
        if (parent) {
            loop.parentHeader = *parent;
        }
    }
    // Populate children lists
    auto findMutableLoop = [&](std::string_view headerLabel) -> Loop * {
        for (auto &candidate : info.loops_) {
            if (candidate.headerLabel == headerLabel)
                return &candidate;
        }
        return nullptr;
    };
    for (auto &loop : info.loops_) {
        if (loop.parentHeader.empty())
            continue;
        if (auto *parentLoop = findMutableLoop(loop.parentHeader))
            parentLoop->childHeaders.push_back(loop.headerLabel);
    }

    // Exits: edges from loop body to outside.
    const auto labelMapIt = cfgCtx.functionLabelToBlock.find(&function);
    const auto *labelMap =
        labelMapIt == cfgCtx.functionLabelToBlock.end() ? nullptr : &labelMapIt->second;
    for (auto &loop : info.loops_) {
        std::vector<LoopExit> exits;
        for (const auto &label : loop.blockLabels) {
            BasicBlock *block = nullptr;
            if (labelMap) {
                auto it = labelMap->find(label);
                if (it != labelMap->end())
                    block = it->second;
            }
            if (!block || block->instructions.empty())
                continue;
            const Instr &term = block->instructions.back();
            for (const auto &succ : term.labels) {
                if (!loop.contains(succ))
                    exits.push_back(LoopExit{label, succ});
            }
        }
        loop.exits = std::move(exits);
    }

    return info;
}

} // namespace il::transform
