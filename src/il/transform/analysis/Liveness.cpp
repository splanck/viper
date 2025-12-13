//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the backwards data-flow analysis used to compute SSA liveness for
// IL functions.  The helpers construct a compact CFG summary, determine the
// register universe, and iterate until fixed point to determine live-in/live-out
// sets per block.
//
//===----------------------------------------------------------------------===//

#include "il/transform/analysis/Liveness.hpp"

#include "il/analysis/CFG.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <utility>

using namespace il::core;

namespace il::transform
{

/// @brief Test whether a value identifier appears in the tracked set.
///
/// @param valueId Dense SSA identifier to query.
/// @return @c true when the bitset contains the identifier.
bool LivenessInfo::SetView::contains(unsigned valueId) const
{
    return bits_ != nullptr && valueId < bits_->size() && (*bits_)[valueId];
}

/// @brief Determine whether the view represents an empty set.
///
/// @return @c true when no bitset is attached or every bit is clear.
bool LivenessInfo::SetView::empty() const
{
    return bits_ == nullptr ||
           std::none_of(bits_->begin(), bits_->end(), [](bool bit) { return bit; });
}

/// @brief Access the underlying bitset describing the view.
///
/// @return Reference to the underlying bitset.
/// @throws Assertion failure when the view is empty.
const std::vector<bool> &LivenessInfo::SetView::bits() const
{
    assert(bits_ && "liveness set view is empty");
    return *bits_;
}

/// @brief Construct a view over an existing bitset pointer.
///
/// @param bits Pointer to the bitset describing the set; may be @c nullptr to
///             represent an empty view.
LivenessInfo::SetView::SetView(const std::vector<bool> *bits) : bits_(bits) {}

/// @brief Retrieve the live-in set for a block.
///
/// @param block Block whose live-in values are requested.
/// @return View over the block's live-in bitset.
LivenessInfo::SetView LivenessInfo::liveIn(const core::BasicBlock &block) const
{
    return liveIn(&block);
}

/// @brief Retrieve the live-in set for an optional block pointer.
///
/// @param block Block pointer or @c nullptr.
/// @return View over the block's live-in bitset or an empty view when @p block is null.
LivenessInfo::SetView LivenessInfo::liveIn(const core::BasicBlock *block) const
{
    if (!block)
        return SetView();
    auto it = blockIndex_.find(block);
    if (it == blockIndex_.end() || it->second >= liveInBits_.size())
        return SetView();
    return SetView(&liveInBits_[it->second]);
}

/// @brief Retrieve the live-out set for a block.
///
/// @param block Block whose live-out values are requested.
/// @return View over the block's live-out bitset.
LivenessInfo::SetView LivenessInfo::liveOut(const core::BasicBlock &block) const
{
    return liveOut(&block);
}

/// @brief Retrieve the live-out set for an optional block pointer.
///
/// @param block Block pointer or @c nullptr.
/// @return View over the block's live-out bitset or an empty view when @p block is null.
LivenessInfo::SetView LivenessInfo::liveOut(const core::BasicBlock *block) const
{
    if (!block)
        return SetView();
    auto it = blockIndex_.find(block);
    if (it == blockIndex_.end() || it->second >= liveOutBits_.size())
        return SetView();
    return SetView(&liveOutBits_[it->second]);
}

/// @brief Report the number of dense SSA identifiers tracked by the analysis.
///
/// @return Value identifier capacity discovered during analysis.
std::size_t LivenessInfo::valueCount() const
{
    return valueCount_;
}

namespace
{
struct BlockInfo
{
    /// @brief Prepare per-block definition/use bitsets sized to @p valueCount.
    explicit BlockInfo(std::size_t valueCount = 0)
        : defs(valueCount, false), uses(valueCount, false)
    {
    }

    std::vector<bool> defs;
    std::vector<bool> uses;
};

/// @brief Determine how many dense SSA identifiers the function may reference.
///
/// Scans function arguments, block parameters, instruction operands, branch
/// arguments, and instruction results to compute the maximum identifier used.
///
/// @param fn Function being analysed.
/// @return Capacity large enough to index every identifier observed in @p fn.
std::size_t determineValueCapacity(const core::Function &fn)
{
    unsigned maxId = 0;
    bool sawId = false;
    auto noteId = [&](unsigned id)
    {
        maxId = std::max(maxId, id);
        sawId = true;
    };

    for (const auto &param : fn.params)
        noteId(param.id);

    for (const auto &block : fn.blocks)
    {
        for (const auto &param : block.params)
            noteId(param.id);

        for (const auto &instr : block.instructions)
        {
            for (const auto &operand : instr.operands)
            {
                if (operand.kind == Value::Kind::Temp)
                    noteId(operand.id);
            }
            for (const auto &argList : instr.brArgs)
            {
                for (const auto &arg : argList)
                {
                    if (arg.kind == Value::Kind::Temp)
                        noteId(arg.id);
                }
            }
            if (instr.result)
                noteId(*instr.result);
        }
    }

    std::size_t capacity = sawId ? static_cast<std::size_t>(maxId) + 1 : 0;
    if (fn.valueNames.size() > capacity)
        capacity = fn.valueNames.size();
    return capacity;
}

/// @brief Mark a bit corresponding to @p id when it falls inside the bitset.
inline void setBit(std::vector<bool> &bits, unsigned id)
{
    assert(id < bits.size());
    if (id < bits.size())
        bits[id] = true;
}

/// @brief Merge set bits from @p src into @p dst.
inline void mergeBits(std::vector<bool> &dst, const std::vector<bool> &src)
{
    assert(dst.size() == src.size());
    for (std::size_t idx = 0; idx < dst.size(); ++idx)
    {
        if (src[idx])
            dst[idx] = true;
    }
}

} // namespace

/// @brief Build a lightweight CFG summary for the function.
///
/// Populates predecessor/successor relationships using the shared CFG helpers so
/// the liveness analysis can avoid full context recomputation.
///
/// @param module Module containing the function.
/// @param fn Function whose CFG summary should be constructed.
/// @return Populated CFG information ready for data-flow analysis.
CFGInfo buildCFG(core::Module &module, core::Function &fn)
{
    CFGInfo info;
    viper::analysis::CFGContext ctx(module);

    for (auto &block : fn.blocks)
    {
        auto &succ = info.successors[&block];
        auto succBlocks = viper::analysis::successors(ctx, block);
        succ.reserve(succBlocks.size());
        for (auto *succBlock : succBlocks)
            succ.push_back(succBlock);
    }

    for (auto &block : fn.blocks)
    {
        auto &pred = info.predecessors[&block];
        auto predBlocks = viper::analysis::predecessors(ctx, block);
        pred.reserve(predBlocks.size());
        for (auto *predBlock : predBlocks)
            pred.push_back(predBlock);
    }

    return info;
}

/// @brief Compute backwards liveness for @p fn using an existing CFG summary.
///
/// @param module Module owning the function (unused but kept for symmetry).
/// @param fn Function being analysed.
/// @param cfg Precomputed CFG summary for @p fn.
/// @return Populated liveness information including live-in/live-out bitsets.
LivenessInfo computeLiveness(core::Module &module, core::Function &fn, const CFGInfo &cfg)
{
    static_cast<void>(module);
    const std::size_t valueCount = determineValueCapacity(fn);

    LivenessInfo info;
    info.valueCount_ = valueCount;
    info.blocks_.reserve(fn.blocks.size());
    info.liveInBits_.assign(fn.blocks.size(), std::vector<bool>(valueCount, false));
    info.liveOutBits_.assign(fn.blocks.size(), std::vector<bool>(valueCount, false));

    std::vector<BlockInfo> blockInfo(fn.blocks.size(), BlockInfo(valueCount));

    for (std::size_t idx = 0; idx < fn.blocks.size(); ++idx)
    {
        auto &block = fn.blocks[idx];
        info.blocks_.push_back(&block);
        info.blockIndex_[&block] = idx;

        BlockInfo state(valueCount);

        for (const auto &param : block.params)
            setBit(state.defs, param.id);

        for (const auto &instr : block.instructions)
        {
            for (const auto &operand : instr.operands)
            {
                if (operand.kind != Value::Kind::Temp)
                    continue;
                const unsigned id = operand.id;
                if (id >= valueCount)
                    continue;
                if (!state.defs[id])
                    state.uses[id] = true;
            }
            for (const auto &argList : instr.brArgs)
            {
                for (const auto &arg : argList)
                {
                    if (arg.kind != Value::Kind::Temp)
                        continue;
                    const unsigned id = arg.id;
                    if (id >= valueCount)
                        continue;
                    if (!state.defs[id])
                        state.uses[id] = true;
                }
            }
            if (instr.result)
                setBit(state.defs, *instr.result);
        }

        blockInfo[idx] = std::move(state);
    }

    std::vector<bool> scratchOut(valueCount, false);
    std::vector<bool> scratchIn(valueCount, false);

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (std::size_t reverseIdx = fn.blocks.size(); reverseIdx-- > 0;)
        {
            const BasicBlock *block = info.blocks_[reverseIdx];
            BlockInfo &state = blockInfo[reverseIdx];

            auto &liveOut = info.liveOutBits_[reverseIdx];
            std::fill(scratchOut.begin(), scratchOut.end(), false);
            auto succIt = cfg.successors.find(block);
            if (succIt != cfg.successors.end())
            {
                for (const BasicBlock *succ : succIt->second)
                {
                    auto succIdxIt = info.blockIndex_.find(succ);
                    if (succIdxIt == info.blockIndex_.end())
                        continue;
                    mergeBits(scratchOut, info.liveInBits_[succIdxIt->second]);
                }
            }
            if (scratchOut != liveOut)
            {
                liveOut = scratchOut;
                changed = true;
            }

            scratchIn = state.uses;
            for (std::size_t idx = 0; idx < valueCount; ++idx)
            {
                if (liveOut[idx] && !state.defs[idx])
                    scratchIn[idx] = true;
            }
            auto &liveIn = info.liveInBits_[reverseIdx];
            if (scratchIn != liveIn)
            {
                liveIn = scratchIn;
                changed = true;
            }
        }
    }

    return info;
}

/// @brief Compute backwards liveness for @p fn, constructing a CFG summary on demand.
///
/// @param module Module containing the function.
/// @param fn Function being analysed.
/// @return Populated liveness information including live-in/live-out bitsets.
LivenessInfo computeLiveness(core::Module &module, core::Function &fn)
{
    CFGInfo cfg = buildCFG(module, fn);
    return computeLiveness(module, fn, cfg);
}

} // namespace il::transform
