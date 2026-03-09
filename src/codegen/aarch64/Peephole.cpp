//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/Peephole.cpp
// Purpose: Driver for conservative peephole optimizations over Machine IR for
//          the AArch64 backend. Delegates to modular sub-passes under
//          peephole/*.cpp for each optimization category.
//
// Key invariants:
// - Rewrites preserve instruction ordering and only substitute encodings that
//   are provably equivalent under the Machine IR conventions.
// - Must be called after register allocation when physical registers are known.
//
// Ownership/Lifetime:
// - Mutates Machine IR graphs owned by the caller without retaining references
//   to transient operands.
//
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Peephole optimization pass driver for the AArch64 code generator.
/// @details Orchestrates modular sub-passes that eliminate redundant moves,
///          fold consecutive register-to-register operations, and apply local
///          rewrites. All patterns are conservative and safe to apply after
///          register allocation.

#include "Peephole.hpp"

#include "peephole/BranchOpt.hpp"
#include "peephole/CopyPropDCE.hpp"
#include "peephole/IdentityElim.hpp"
#include "peephole/LoopOpt.hpp"
#include "peephole/MemoryOpt.hpp"
#include "peephole/PeepholeCommon.hpp"
#include "peephole/StrengthReduce.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64
{

// Import sub-pass functions into local scope for concise call sites.
namespace ph = peephole;

PeepholeStats runPeephole(MFunction &fn)
{
    PeepholeStats stats;

    // Pass 0: Reorder blocks for better code layout
    stats.blocksReordered = static_cast<int>(ph::reorderBlocks(fn));

    // Pass 0.5: Hoist loop-invariant MovRI out of loop bodies
    stats.loopConstsHoisted = static_cast<int>(ph::hoistLoopConstants(fn));

    for (auto &block : fn.blocks)
    {
        auto &instrs = block.instrs;
        if (instrs.empty())
            continue;

        // Pass 1: Build register constant map and apply rewrites
        ph::RegConstMap knownConsts;
        for (auto &instr : instrs)
        {
            // Track constants loaded via MovRI
            ph::updateKnownConsts(instr, knownConsts);

            // Try cmp reg, #0 -> tst reg, reg
            if (ph::tryCmpZeroToTst(instr, stats))
                continue;

            // Try arithmetic identity elimination (add #0, sub #0, shift #0)
            if (ph::tryArithmeticIdentity(instr, stats))
                continue;

            // Try strength reduction (mul power-of-2 -> shift, udiv power-of-2 -> lsr)
            (void)ph::tryStrengthReduction(instr, knownConsts, stats);
            (void)ph::tryDivStrengthReduction(instr, knownConsts, stats);

            // Try immediate folding (add/sub RRR -> RI when operand is known const)
            (void)ph::tryImmediateFolding(instr, knownConsts, stats);
        }

        // Pass 1.5: Copy propagation - replace uses with original sources
        ph::propagateCopies(instrs, stats);

        // Pass 1.6: CBZ/CBNZ fusion (cmp #0 + b.eq/ne -> cbz/cbnz)
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
        {
            if (ph::tryCbzCbnzFusion(instrs, i, stats))
            {
                if (i > 0)
                    --i;
            }
        }

        // Pass 1.65: Cset+cbnz/cbz -> b.cond fusion
        for (std::size_t i = 0; i < instrs.size(); ++i)
        {
            if (ph::tryCsetBranchFusion(instrs, i, stats))
            {
                if (i > 0)
                    --i;
            }
        }

        // Pass 1.7: MADD fusion (mul + add -> madd)
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
        {
            if (ph::tryMaddFusion(instrs, i, stats))
            {
                if (i > 0)
                    --i;
            }
        }

        // Pass 1.8: LDP/STP merging (consecutive ldr/str with adjacent offsets)
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
        {
            if (ph::tryLdpStpMerge(instrs, i, stats))
            {
                if (i > 0)
                    --i;
            }
        }

        // Pass 1.85: Dead FP store elimination (str+str to same offset -> remove earlier)
        ph::eliminateDeadFpStores(instrs, stats);

        // Pass 1.9: Store-load forwarding (str+ldr at same FP offset -> mov)
        ph::forwardStoreLoads(instrs, stats);

        // Pass 1.97: Compute-into-target fold (op Rd, ...; mov Rt, Rd -> op Rt, ...)
        ph::foldComputeIntoTarget(instrs, stats);

        // Pass 2: Try to fold consecutive moves (including imm-then-move)
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
        {
            if (!ph::tryFoldImmThenMove(instrs, i, stats))
                (void)ph::tryFoldConsecutiveMoves(instrs, i, stats);
        }

        // Pass 3: Mark identity moves for removal
        std::vector<bool> toRemove(instrs.size(), false);

        for (std::size_t i = 0; i < instrs.size(); ++i)
        {
            if (ph::isIdentityMovRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityMovesRemoved;
            }
            else if (ph::isIdentityFMovRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityFMovesRemoved;
            }
        }

        // Pass 4: Remove marked instructions
        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool v) { return v; }))
        {
            ph::removeMarkedInstructions(instrs, toRemove);
        }

        // Pass 4.5: Dead code elimination - remove instructions with unused results
        ph::removeDeadInstructions(instrs, stats);

        // Pass 4.6: Dead flag-setter elimination -- must run AFTER general DCE
        // so that dead Cset/Csel instructions (which read flags) are removed
        // first, exposing flag-setters whose results are truly unused.
        ph::removeDeadFlagSetters(instrs, stats);
    }

    // Pass 4.8: Cross-block store-load forwarding for phi stores/loads.
    // When block A ends with str Rx, [fp, #off] and its layout successor B
    // starts with ldr Ry, [fp, #off], replace the load with mov Ry, Rx.
    // This eliminates the store->load round-trip through the stack for block
    // parameter passing (phi stores/loads from IL block params).
    //
    // SAFETY: Only forward when block B has exactly ONE predecessor (block A).
    // If B has multiple predecessors, different paths may store different values
    // to the same FP offset.
    {
        // Build predecessor count for each block from branch targets.
        std::unordered_map<std::string, std::size_t> predCount;
        for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
        {
            for (const auto &mi : fn.blocks[bi].instrs)
            {
                if (mi.opc == MOpcode::Br && !mi.ops.empty() &&
                    mi.ops[0].kind == MOperand::Kind::Label)
                    ++predCount[mi.ops[0].label];
                else if (mi.opc == MOpcode::BCond && mi.ops.size() >= 2 &&
                         mi.ops[1].kind == MOperand::Kind::Label)
                    ++predCount[mi.ops[1].label];
                else if ((mi.opc == MOpcode::Cbz || mi.opc == MOpcode::Cbnz) &&
                         mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label)
                    ++predCount[mi.ops[1].label];
            }
        }

        for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi)
        {
            auto &predInstrs = fn.blocks[bi].instrs;
            auto &succBlock = fn.blocks[bi + 1];
            auto &succInstrs = succBlock.instrs;

            if (predInstrs.empty() || succInstrs.empty())
                continue;

            // Only forward to blocks with exactly one predecessor
            auto pcIt = predCount.find(succBlock.name);
            if (pcIt == predCount.end() || pcIt->second != 1)
                continue;

            // Collect stores at the end of the predecessor block.
            struct StoreInfo
            {
                std::size_t idx;
                MOperand srcReg;
            };
            std::unordered_map<int64_t, StoreInfo> endStores;

            for (std::size_t i = predInstrs.size(); i-- > 0;)
            {
                const auto &instr = predInstrs[i];

                // Skip terminators
                if (instr.opc == MOpcode::Br || instr.opc == MOpcode::BCond ||
                    instr.opc == MOpcode::Ret || instr.opc == MOpcode::Cbz ||
                    instr.opc == MOpcode::Cbnz)
                    continue;

                // Record FP-relative stores
                if (instr.opc == MOpcode::StrRegFpImm && instr.ops.size() >= 2 &&
                    ph::isPhysReg(instr.ops[0]) && instr.ops[1].kind == MOperand::Kind::Imm)
                {
                    const int64_t off = instr.ops[1].imm;
                    if (endStores.find(off) == endStores.end())
                        endStores[off] = {i, instr.ops[0]};
                    continue;
                }

                // Stop scanning at non-store, non-terminator
                break;
            }

            if (endStores.empty())
                continue;

            // Forward to loads at the start of the successor block.
            for (std::size_t j = 0; j < succInstrs.size(); ++j)
            {
                const auto &instr = succInstrs[j];

                if (instr.opc != MOpcode::LdrRegFpImm)
                    break;
                if (instr.ops.size() < 2 || !ph::isPhysReg(instr.ops[0]) ||
                    instr.ops[1].kind != MOperand::Kind::Imm)
                    break;

                const int64_t off = instr.ops[1].imm;
                auto it = endStores.find(off);
                if (it == endStores.end())
                    continue;

                // Replace load with mov
                succInstrs[j] = MInstr{MOpcode::MovRR, {instr.ops[0], it->second.srcReg}};
                ++stats.deadInstructionsRemoved;
            }
        }
    }

    // Pass 5: Branch inversion and branch-to-next removal.
    // This must be done after per-block passes since it looks at adjacent blocks.
    for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi)
    {
        auto &block = fn.blocks[bi];
        const auto &nextBlock = fn.blocks[bi + 1];

        if (block.instrs.empty())
            continue;

        // Branch inversion: b.cond .Ltarget; b .Lfallthrough
        // when .Ltarget == next block -> b.!cond .Lfallthrough (remove b .Lfallthrough)
        if (block.instrs.size() >= 2)
        {
            auto &secondLast = block.instrs[block.instrs.size() - 2];
            auto &last = block.instrs[block.instrs.size() - 1];

            if (secondLast.opc == MOpcode::BCond && secondLast.ops.size() == 2 &&
                secondLast.ops[0].kind == MOperand::Kind::Cond &&
                secondLast.ops[1].kind == MOperand::Kind::Label && last.opc == MOpcode::Br &&
                last.ops.size() == 1 && last.ops[0].kind == MOperand::Kind::Label)
            {
                if (secondLast.ops[1].label == nextBlock.name)
                {
                    const char *inv = ph::invertCondition(secondLast.ops[0].cond);
                    if (inv)
                    {
                        secondLast.ops[0] = MOperand::condOp(inv);
                        secondLast.ops[1] = last.ops[0];
                        block.instrs.pop_back();
                        ++stats.branchInversions;
                        continue;
                    }
                }
            }
        }

        // Remove branches to the immediately following block
        auto &lastInstr = block.instrs.back();
        if (ph::isBranchTo(lastInstr, nextBlock.name))
        {
            block.instrs.pop_back();
            ++stats.branchesToNextRemoved;
        }
    }

    return stats;
}

void pruneUnusedCalleeSaved(MFunction &fn)
{
    // Build a set of all physical registers actually referenced in the MIR.
    std::unordered_set<uint16_t> usedRegs;
    for (const auto &bb : fn.blocks)
    {
        for (const auto &mi : bb.instrs)
        {
            for (const auto &op : mi.ops)
            {
                if (op.kind == MOperand::Kind::Reg && op.reg.isPhys)
                    usedRegs.insert(op.reg.idOrPhys);
            }
        }
    }

    // Prune savedGPRs: remove any callee-saved register not referenced.
    fn.savedGPRs.erase(
        std::remove_if(fn.savedGPRs.begin(),
                       fn.savedGPRs.end(),
                       [&](PhysReg r)
                       { return usedRegs.find(static_cast<uint16_t>(r)) == usedRegs.end(); }),
        fn.savedGPRs.end());

    // Prune savedFPRs: same logic.
    fn.savedFPRs.erase(
        std::remove_if(fn.savedFPRs.begin(),
                       fn.savedFPRs.end(),
                       [&](PhysReg r)
                       { return usedRegs.find(static_cast<uint16_t>(r)) == usedRegs.end(); }),
        fn.savedFPRs.end());
}

} // namespace viper::codegen::aarch64
