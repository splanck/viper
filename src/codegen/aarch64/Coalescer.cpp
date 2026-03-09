//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/Coalescer.cpp
// Purpose: Pre-register-allocation move coalescer for AArch64 MIR.
//          Eliminates redundant MovRR/FMovRR between virtual registers by
//          merging vregs whose live ranges do not interfere.
// Key invariants:
//   - Only coalesces moves where both operands are virtual registers.
//   - After coalescing, all references to the replaced vreg are rewritten.
//   - The coalesced move instruction is replaced with a no-op (identity move)
//     which is later cleaned up by the peephole pass.
// Ownership/Lifetime:
//   - Modifies MFunction in place; caller owns the MFunction.
// Links: codegen/aarch64/Coalescer.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/Coalescer.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::aarch64
{
namespace
{

/// @brief Represents a half-open live interval [start, end) for a virtual register.
struct LiveInterval
{
    unsigned start{0};
    unsigned end{0};
};

/// @brief Candidate move instruction eligible for coalescing.
struct CoalesceCandidate
{
    std::size_t blockIdx; ///< Block containing the move.
    std::size_t instrIdx; ///< Index within the block's instruction list.
    uint16_t dstVReg;     ///< Destination virtual register ID.
    uint16_t srcVReg;     ///< Source virtual register ID.
    RegClass cls;         ///< Register class (GPR or FPR).
};

/// @brief Check if an opcode is a virtual-to-virtual register move.
/// @param opc The machine opcode.
/// @return True for MovRR or FMovRR.
static bool isMoveRR(MOpcode opc)
{
    return opc == MOpcode::MovRR || opc == MOpcode::FMovRR;
}

/// @brief Assign a globally unique instruction index to each instruction.
///
/// Walks all blocks in layout order, assigning sequential indices. This
/// produces a linearized view of the function for live interval computation.
///
/// @param fn The machine function.
/// @return Map from (blockIdx, instrIdx) encoded as a single key to global index.
static unsigned linearizeFunction(const MFunction &fn,
                                  std::vector<std::vector<unsigned>> &instrIndex)
{
    instrIndex.clear();
    instrIndex.resize(fn.blocks.size());
    unsigned idx = 0;
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
    {
        instrIndex[bi].resize(fn.blocks[bi].instrs.size());
        for (std::size_t ii = 0; ii < fn.blocks[bi].instrs.size(); ++ii)
        {
            instrIndex[bi][ii] = idx++;
        }
    }
    return idx;
}

/// @brief Compute conservative live intervals for all virtual registers.
///
/// For each vreg, the interval spans from its first definition to its last use
/// across the entire function (linearized). This is conservative because it does
/// not account for control flow -- it treats the function as a straight-line
/// sequence of instructions.
///
/// @param fn The machine function.
/// @param instrIndex Linearized instruction indices from linearizeFunction.
/// @param totalInstrs Total instruction count.
/// @param cls Filter: only compute intervals for vregs of this register class.
/// @return Map from vreg ID to its live interval.
static std::unordered_map<uint16_t, LiveInterval> computeLiveIntervals(
    const MFunction &fn,
    const std::vector<std::vector<unsigned>> &instrIndex,
    unsigned totalInstrs,
    RegClass cls)
{
    (void)totalInstrs;
    std::unordered_map<uint16_t, LiveInterval> intervals;

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
    {
        for (std::size_t ii = 0; ii < fn.blocks[bi].instrs.size(); ++ii)
        {
            const auto &mi = fn.blocks[bi].instrs[ii];
            const unsigned pos = instrIndex[bi][ii];

            for (const auto &op : mi.ops)
            {
                if (op.kind != MOperand::Kind::Reg)
                    continue;
                if (op.reg.isPhys)
                    continue;
                if (op.reg.cls != cls)
                    continue;

                const uint16_t vreg = op.reg.idOrPhys;
                auto it = intervals.find(vreg);
                if (it == intervals.end())
                {
                    intervals[vreg] = LiveInterval{pos, pos + 1};
                }
                else
                {
                    if (pos < it->second.start)
                        it->second.start = pos;
                    if (pos + 1 > it->second.end)
                        it->second.end = pos + 1;
                }
            }
        }
    }

    return intervals;
}

/// @brief Check if two live intervals overlap.
static bool interferes(const LiveInterval &a, const LiveInterval &b)
{
    return a.start < b.end && b.start < a.end;
}

/// @brief Rewrite all references to oldVReg as newVReg throughout the function.
static void rewriteVReg(MFunction &fn, RegClass cls, uint16_t oldVReg, uint16_t newVReg)
{
    for (auto &bb : fn.blocks)
    {
        for (auto &mi : bb.instrs)
        {
            for (auto &op : mi.ops)
            {
                if (op.kind == MOperand::Kind::Reg && !op.reg.isPhys && op.reg.cls == cls &&
                    op.reg.idOrPhys == oldVReg)
                {
                    op.reg.idOrPhys = newVReg;
                }
            }
        }
    }
}

/// @brief Collect candidate move instructions for coalescing.
static std::vector<CoalesceCandidate> collectCandidates(const MFunction &fn)
{
    std::vector<CoalesceCandidate> candidates;

    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
    {
        for (std::size_t ii = 0; ii < fn.blocks[bi].instrs.size(); ++ii)
        {
            const auto &mi = fn.blocks[bi].instrs[ii];
            if (!isMoveRR(mi.opc))
                continue;
            if (mi.ops.size() < 2)
                continue;

            const auto &dst = mi.ops[0];
            const auto &src = mi.ops[1];

            // Both must be virtual registers of the same class.
            if (dst.kind != MOperand::Kind::Reg || src.kind != MOperand::Kind::Reg)
                continue;
            if (dst.reg.isPhys || src.reg.isPhys)
                continue;
            if (dst.reg.cls != src.reg.cls)
                continue;
            // Skip identity moves.
            if (dst.reg.idOrPhys == src.reg.idOrPhys)
                continue;

            candidates.push_back(
                CoalesceCandidate{bi, ii, dst.reg.idOrPhys, src.reg.idOrPhys, dst.reg.cls});
        }
    }

    return candidates;
}

/// @brief Run one round of coalescing for a given register class.
/// @return The number of moves coalesced.
static unsigned coalesceClass(MFunction &fn, RegClass cls)
{
    unsigned coalesced = 0;

    // Recompute intervals and candidates each iteration since coalescing
    // changes live ranges.
    bool changed = true;
    while (changed)
    {
        changed = false;

        std::vector<std::vector<unsigned>> instrIndex;
        const unsigned totalInstrs = linearizeFunction(fn, instrIndex);
        auto intervals = computeLiveIntervals(fn, instrIndex, totalInstrs, cls);
        auto candidates = collectCandidates(fn);

        for (const auto &cand : candidates)
        {
            if (cand.cls != cls)
                continue;

            auto dstIt = intervals.find(cand.dstVReg);
            auto srcIt = intervals.find(cand.srcVReg);
            if (dstIt == intervals.end() || srcIt == intervals.end())
                continue;

            // The move defines dstVReg from srcVReg. For coalescing, we need
            // to check whether the *merged* interval would conflict. Since we
            // are replacing dstVReg with srcVReg, the srcVReg's interval
            // extends to cover dstVReg's range. We check: does dstVReg's range
            // (excluding the move point itself) overlap with srcVReg's range?
            //
            // Conservative check: the src interval, extended by the dst
            // interval, must not interfere with any *other* vreg that already
            // interferes with only one of them. But a simpler, still-sound
            // check: if the src and dst intervals only overlap at the move
            // point itself, coalescing is safe.
            //
            // We use the simple non-interference check: the src live range
            // (excluding the definition of dst at the move point) must not
            // overlap with dst's live range.

            const unsigned movePos = instrIndex[cand.blockIdx][cand.instrIdx];

            // Build the effective src interval excluding the move point.
            // The src is used at movePos (as a read), and the dst is defined
            // at movePos. After coalescing, the merged vreg would span
            // min(src.start, dst.start) to max(src.end, dst.end).
            // The coalescing is safe if the only overlap point between src and
            // dst intervals is the move instruction itself.

            LiveInterval srcRange = srcIt->second;
            LiveInterval dstRange = dstIt->second;

            // If dst is only defined at the move and used afterwards, and src
            // is defined before and last used at the move, there is no
            // interference. More generally: check that the intervals only share
            // the move point.

            // Quick interference check: if the src's range ends at or before
            // the dst's range starts (at the move), they don't interfere.
            // Similarly if dst ends before src starts. Otherwise, check if
            // the overlap is strictly at the move point.

            // Conservative: if removing the move point from consideration,
            // do the intervals still overlap?
            bool safeToCoalesce = false;

            if (srcRange.end <= movePos + 1 && dstRange.start >= movePos)
            {
                // src ends at or right after the move, dst starts at the move.
                // No conflict after removing the move.
                safeToCoalesce = true;
            }
            else if (!interferes(srcRange, dstRange))
            {
                // No overlap at all.
                safeToCoalesce = true;
            }
            else
            {
                // Overlap exists beyond the move point. Check if the overlap
                // region is exactly [movePos, movePos+1).
                unsigned overlapStart = std::max(srcRange.start, dstRange.start);
                unsigned overlapEnd = std::min(srcRange.end, dstRange.end);
                if (overlapStart == movePos && overlapEnd == movePos + 1)
                    safeToCoalesce = true;
            }

            if (!safeToCoalesce)
                continue;

            // Coalesce: rewrite all dstVReg references to srcVReg.
            rewriteVReg(fn, cls, cand.dstVReg, cand.srcVReg);

            // The move is now an identity move (srcVReg -> srcVReg).
            // Mark it for deletion by converting to identity.
            // The peephole pass or regalloc will clean these up.
            // We leave it as-is since rewriteVReg already made both
            // operands the same vreg.

            ++coalesced;
            changed = true;
            break; // Restart with fresh intervals after each coalesce.
        }
    }

    return coalesced;
}

/// @brief Remove identity moves (MovRR/FMovRR where src == dst vreg).
static void removeIdentityMoves(MFunction &fn)
{
    for (auto &bb : fn.blocks)
    {
        std::vector<MInstr> filtered;
        filtered.reserve(bb.instrs.size());
        for (auto &mi : bb.instrs)
        {
            if (isMoveRR(mi.opc) && mi.ops.size() >= 2 && mi.ops[0].kind == MOperand::Kind::Reg &&
                mi.ops[1].kind == MOperand::Kind::Reg && !mi.ops[0].reg.isPhys &&
                !mi.ops[1].reg.isPhys && mi.ops[0].reg.idOrPhys == mi.ops[1].reg.idOrPhys)
            {
                continue; // Skip identity move.
            }
            filtered.push_back(std::move(mi));
        }
        bb.instrs = std::move(filtered);
    }
}

} // namespace

void coalesce(MFunction &fn)
{
    // Coalesce GPR and FPR classes independently.
    coalesceClass(fn, RegClass::GPR);
    coalesceClass(fn, RegClass::FPR);

    // Clean up identity moves left behind by coalescing.
    removeIdentityMoves(fn);
}

} // namespace viper::codegen::aarch64
