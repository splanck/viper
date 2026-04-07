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

namespace viper::codegen::aarch64 {

// Import sub-pass functions into local scope for concise call sites.
namespace ph = peephole;

namespace {

struct JoinLoad {
    std::size_t instrIndex;
    int64_t offset;
    MOperand dstReg;
    RegClass cls;
};

struct JoinStore {
    std::size_t instrIndex;
    int64_t offset;
    MOperand srcReg;
    RegClass cls;
};

struct JoinCopy {
    MOperand srcReg;
    MOperand dstReg;
    RegClass cls;
};

static std::uint32_t regKey(const MOperand &op) {
    return (static_cast<std::uint32_t>(op.reg.cls) << 16) |
           static_cast<std::uint32_t>(op.reg.idOrPhys);
}

static bool blockFallsThroughTo(const MFunction &fn, std::size_t blockIndex, const std::string &succName) {
    if (blockIndex + 1 >= fn.blocks.size())
        return false;
    if (fn.blocks[blockIndex + 1].name != succName)
        return false;
    const auto &instrs = fn.blocks[blockIndex].instrs;
    if (instrs.empty())
        return true;
    const auto &last = instrs.back();
    return last.opc != MOpcode::Br && last.opc != MOpcode::BCond && last.opc != MOpcode::Cbz &&
           last.opc != MOpcode::Cbnz && last.opc != MOpcode::Ret;
}

static std::unordered_map<std::string, std::vector<std::size_t>> buildPredecessorMap(const MFunction &fn) {
    std::unordered_map<std::string, std::vector<std::size_t>> preds;
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        const auto &block = fn.blocks[bi];
        if (block.instrs.empty()) {
            if (bi + 1 < fn.blocks.size())
                preds[fn.blocks[bi + 1].name].push_back(bi);
            continue;
        }

        const auto &last = block.instrs.back();
        if (last.opc == MOpcode::Br && !last.ops.empty() &&
            last.ops[0].kind == MOperand::Kind::Label) {
            preds[last.ops[0].label].push_back(bi);
            continue;
        }

        if ((last.opc == MOpcode::BCond || last.opc == MOpcode::Cbz || last.opc == MOpcode::Cbnz) &&
            last.ops.size() >= 2 && last.ops[1].kind == MOperand::Kind::Label) {
            preds[last.ops[1].label].push_back(bi);
            if (bi + 1 < fn.blocks.size())
                preds[fn.blocks[bi + 1].name].push_back(bi);
            continue;
        }

        if (last.opc != MOpcode::Ret && bi + 1 < fn.blocks.size())
            preds[fn.blocks[bi + 1].name].push_back(bi);
    }
    return preds;
}

static bool isDirectPredEdgeTo(const MFunction &fn, std::size_t predIndex, const std::string &succName) {
    const auto &instrs = fn.blocks[predIndex].instrs;
    if (instrs.empty())
        return blockFallsThroughTo(fn, predIndex, succName);
    const auto &last = instrs.back();
    if (last.opc == MOpcode::Br && !last.ops.empty() && last.ops[0].kind == MOperand::Kind::Label)
        return last.ops[0].label == succName;
    return blockFallsThroughTo(fn, predIndex, succName);
}

static bool canInsertJoinCopiesOnPredEdge(const MFunction &fn,
                                          std::size_t predIndex,
                                          const std::string &succName) {
    const auto &instrs = fn.blocks[predIndex].instrs;
    if (instrs.empty())
        return blockFallsThroughTo(fn, predIndex, succName);
    const auto &last = instrs.back();
    if (last.opc == MOpcode::Br && !last.ops.empty() && last.ops[0].kind == MOperand::Kind::Label)
        return last.ops[0].label == succName;
    if (last.opc == MOpcode::BCond || last.opc == MOpcode::Cbz || last.opc == MOpcode::Cbnz ||
        last.opc == MOpcode::Ret)
        return false;
    return blockFallsThroughTo(fn, predIndex, succName);
}

static bool collectJoinPrefixLoads(const MBasicBlock &block, std::vector<JoinLoad> &loads) {
    loads.clear();
    for (std::size_t i = 0; i < block.instrs.size(); ++i) {
        const auto &instr = block.instrs[i];
        switch (instr.opc) {
            case MOpcode::LdrRegFpImm:
            case MOpcode::LdrFprFpImm:
                if (instr.ops.size() < 2 || !ph::isPhysReg(instr.ops[0]) ||
                    instr.ops[1].kind != MOperand::Kind::Imm)
                    return !loads.empty();
                loads.push_back({i,
                                 instr.ops[1].imm,
                                 instr.ops[0],
                                 instr.opc == MOpcode::LdrFprFpImm ? RegClass::FPR
                                                                   : RegClass::GPR});
                continue;
            case MOpcode::LdpRegFpImm:
            case MOpcode::LdpFprFpImm:
                if (instr.ops.size() < 3 || !ph::isPhysReg(instr.ops[0]) ||
                    !ph::isPhysReg(instr.ops[1]) || instr.ops[2].kind != MOperand::Kind::Imm)
                    return !loads.empty();
                loads.push_back({i,
                                 instr.ops[2].imm,
                                 instr.ops[0],
                                 instr.opc == MOpcode::LdpFprFpImm ? RegClass::FPR
                                                                   : RegClass::GPR});
                loads.push_back({i,
                                 instr.ops[2].imm + 8,
                                 instr.ops[1],
                                 instr.opc == MOpcode::LdpFprFpImm ? RegClass::FPR
                                                                   : RegClass::GPR});
                continue;
            default:
                return !loads.empty();
        }
    }
    return !loads.empty();
}

static bool collectJoinSuffixStores(const MFunction &fn,
                                    std::size_t predIndex,
                                    const std::string &succName,
                                    std::unordered_map<int64_t, JoinStore> &stores,
                                    std::unordered_set<std::size_t> &storeInstrs) {
    stores.clear();
    storeInstrs.clear();
    if (!isDirectPredEdgeTo(fn, predIndex, succName))
        return false;

    const auto &instrs = fn.blocks[predIndex].instrs;
    std::size_t scanEnd = instrs.size();
    if (!instrs.empty()) {
        const auto &last = instrs.back();
        if (last.opc == MOpcode::Br && !last.ops.empty() && last.ops[0].kind == MOperand::Kind::Label &&
            last.ops[0].label == succName)
            scanEnd = instrs.size() - 1;
    }

    std::unordered_set<std::uint32_t> clobbered;
    for (std::size_t i = scanEnd; i-- > 0;) {
        const auto &instr = instrs[i];
        if ((instr.opc == MOpcode::LdrRegFpImm || instr.opc == MOpcode::LdrFprFpImm ||
             instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm ||
             instr.opc == MOpcode::MovRR || instr.opc == MOpcode::FMovRR) &&
            i + 1 <= scanEnd) {
            if (instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) {
                if (instr.ops.size() >= 1 && ph::isPhysReg(instr.ops[0]))
                    clobbered.insert(regKey(instr.ops[0]));
                if (instr.ops.size() >= 2 && ph::isPhysReg(instr.ops[1]))
                    clobbered.insert(regKey(instr.ops[1]));
            } else if (auto def = ph::getDefinedReg(instr); def && ph::isPhysReg(*def)) {
                clobbered.insert(regKey(*def));
            }
            continue;
        }

        if ((instr.opc == MOpcode::StrRegFpImm || instr.opc == MOpcode::StrFprFpImm) &&
            instr.ops.size() >= 2 && ph::isPhysReg(instr.ops[0]) &&
            instr.ops[1].kind == MOperand::Kind::Imm) {
            const auto key = regKey(instr.ops[0]);
            const int64_t off = instr.ops[1].imm;
            if (!clobbered.count(key) && stores.find(off) == stores.end()) {
                stores.emplace(off,
                               JoinStore{i,
                                         off,
                                         instr.ops[0],
                                         instr.opc == MOpcode::StrFprFpImm ? RegClass::FPR
                                                                            : RegClass::GPR});
                storeInstrs.insert(i);
            }
            continue;
        }

        break;
    }

    return !stores.empty();
}

static bool orderJoinCopies(const std::vector<JoinCopy> &copies, std::vector<JoinCopy> &ordered) {
    ordered.clear();
    ordered.reserve(copies.size());

    std::vector<JoinCopy> pending;
    pending.reserve(copies.size());
    for (const auto &copy : copies) {
        if (copy.srcReg.reg.idOrPhys == copy.dstReg.reg.idOrPhys)
            continue;
        pending.push_back(copy);
    }

    while (!pending.empty()) {
        auto readyIt = pending.end();
        for (auto it = pending.begin(); it != pending.end(); ++it) {
            const auto dstPhys = it->dstReg.reg.idOrPhys;
            bool dstUsedAsSource = false;
            for (auto jt = pending.begin(); jt != pending.end(); ++jt) {
                if (it == jt)
                    continue;
                if (jt->cls == it->cls && jt->srcReg.reg.idOrPhys == dstPhys) {
                    dstUsedAsSource = true;
                    break;
                }
            }
            if (!dstUsedAsSource) {
                readyIt = it;
                break;
            }
        }
        if (readyIt == pending.end())
            return false;
        ordered.push_back(*readyIt);
        pending.erase(readyIt);
    }

    return true;
}

static void markInstructionDefs(const MInstr &instr, std::unordered_set<std::uint32_t> &clobbered) {
    if (instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) {
        if (instr.ops.size() >= 1 && ph::isPhysReg(instr.ops[0]))
            clobbered.insert(regKey(instr.ops[0]));
        if (instr.ops.size() >= 2 && ph::isPhysReg(instr.ops[1]))
            clobbered.insert(regKey(instr.ops[1]));
        return;
    }

    if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr) {
        for (uint16_t reg = static_cast<uint16_t>(PhysReg::X0);
             reg <= static_cast<uint16_t>(PhysReg::X18);
             ++reg)
            clobbered.insert((static_cast<std::uint32_t>(RegClass::GPR) << 16) | reg);
        for (uint16_t reg = static_cast<uint16_t>(PhysReg::V0);
             reg <= static_cast<uint16_t>(PhysReg::V31);
             ++reg)
            clobbered.insert((static_cast<std::uint32_t>(RegClass::FPR) << 16) | reg);
        return;
    }

    if (auto def = ph::getDefinedReg(instr); def && ph::isPhysReg(*def))
        clobbered.insert(regKey(*def));
}

static bool forwardSinglePredPhiLoads(MFunction &fn, PeepholeStats &stats) {
    bool changed = false;
    const auto preds = buildPredecessorMap(fn);

    for (std::size_t blockIndex = 0; blockIndex < fn.blocks.size(); ++blockIndex) {
        auto &block = fn.blocks[blockIndex];
        std::vector<JoinLoad> loads;
        if (!collectJoinPrefixLoads(block, loads))
            continue;

        auto predIt = preds.find(block.name);
        if (predIt == preds.end() || predIt->second.size() != 1)
            continue;

        const std::size_t predIndex = predIt->second.front();
        if (predIndex >= blockIndex)
            continue;
        if (!isDirectPredEdgeTo(fn, predIndex, block.name))
            continue;

        std::unordered_map<int64_t, JoinStore> stores;
        std::unordered_set<std::size_t> ignoredStoreInstrs;
        if (!collectJoinSuffixStores(fn, predIndex, block.name, stores, ignoredStoreInstrs))
            continue;

        std::vector<JoinCopy> copies;
        copies.reserve(loads.size());
        for (const auto &load : loads) {
            auto storeIt = stores.find(load.offset);
            if (storeIt == stores.end() || storeIt->second.cls != load.cls) {
                copies.clear();
                break;
            }
            copies.push_back({storeIt->second.srcReg, load.dstReg, load.cls});
        }
        if (copies.empty())
            continue;

        std::vector<JoinCopy> ordered;
        if (!orderJoinCopies(copies, ordered))
            continue;

        std::vector<bool> removeLoadInstr(block.instrs.size(), false);
        for (const auto &load : loads)
            removeLoadInstr[load.instrIndex] = true;

        std::vector<MInstr> rewritten;
        rewritten.reserve(block.instrs.size() - std::count(removeLoadInstr.begin(),
                                                           removeLoadInstr.end(),
                                                           true) +
                          ordered.size());
        bool insertedMoves = false;
        for (std::size_t ii = 0; ii < block.instrs.size(); ++ii) {
            if (removeLoadInstr[ii]) {
                if (!insertedMoves) {
                    for (const auto &copy : ordered) {
                        rewritten.push_back(MInstr{copy.cls == RegClass::FPR ? MOpcode::FMovRR
                                                                             : MOpcode::MovRR,
                                                   {copy.dstReg, copy.srcReg}});
                    }
                    insertedMoves = true;
                }
                continue;
            }
            rewritten.push_back(block.instrs[ii]);
        }
        block.instrs.swap(rewritten);
        stats.deadInstructionsRemoved +=
            static_cast<int>(std::count(removeLoadInstr.begin(), removeLoadInstr.end(), true));
        changed = true;
    }

    return changed;
}

static bool coalesceJoinPhiLoads(MFunction &fn, PeepholeStats &stats) {
    bool changed = false;
    const auto preds = buildPredecessorMap(fn);

    for (auto &block : fn.blocks) {
        std::vector<JoinLoad> loads;
        if (!collectJoinPrefixLoads(block, loads))
            continue;

        auto predIt = preds.find(block.name);
        if (predIt == preds.end() || predIt->second.size() < 2)
            continue;

        std::vector<std::vector<JoinCopy>> predCopies;
        predCopies.reserve(predIt->second.size());
        bool allPredsConvertible = true;
        for (std::size_t predIndex : predIt->second) {
            if (!canInsertJoinCopiesOnPredEdge(fn, predIndex, block.name)) {
                allPredsConvertible = false;
                break;
            }

            std::unordered_map<int64_t, JoinStore> stores;
            std::unordered_set<std::size_t> ignoredStoreInstrs;
            if (!collectJoinSuffixStores(fn, predIndex, block.name, stores, ignoredStoreInstrs)) {
                allPredsConvertible = false;
                break;
            }

            std::vector<JoinCopy> copies;
            copies.reserve(loads.size());
            for (const auto &load : loads) {
                auto storeIt = stores.find(load.offset);
                if (storeIt == stores.end() || storeIt->second.cls != load.cls) {
                    allPredsConvertible = false;
                    break;
                }
                copies.push_back({storeIt->second.srcReg, load.dstReg, load.cls});
            }
            if (!allPredsConvertible)
                break;

            std::vector<JoinCopy> ordered;
            if (!orderJoinCopies(copies, ordered)) {
                allPredsConvertible = false;
                break;
            }
            predCopies.push_back(std::move(ordered));
        }

        if (!allPredsConvertible || predCopies.empty())
            continue;

        for (std::size_t pi = 0; pi < predIt->second.size(); ++pi) {
            auto &predBlock = fn.blocks[predIt->second[pi]];
            const bool branchesDirectly = !predBlock.instrs.empty() &&
                                          predBlock.instrs.back().opc == MOpcode::Br &&
                                          predBlock.instrs.back().ops.size() == 1 &&
                                          predBlock.instrs.back().ops[0].kind ==
                                              MOperand::Kind::Label &&
                                          predBlock.instrs.back().ops[0].label == block.name;
            const std::size_t insertAt =
                branchesDirectly ? predBlock.instrs.size() - 1 : predBlock.instrs.size();

            std::vector<MInstr> rewritten;
            rewritten.reserve(predBlock.instrs.size() + predCopies[pi].size());
            for (std::size_t ii = 0; ii < predBlock.instrs.size(); ++ii) {
                if (ii == insertAt) {
                    for (const auto &copy : predCopies[pi]) {
                        rewritten.push_back(MInstr{copy.cls == RegClass::FPR ? MOpcode::FMovRR
                                                                             : MOpcode::MovRR,
                                                   {copy.dstReg, copy.srcReg}});
                    }
                }
                rewritten.push_back(predBlock.instrs[ii]);
            }
            if (insertAt == predBlock.instrs.size()) {
                for (const auto &copy : predCopies[pi]) {
                    rewritten.push_back(MInstr{copy.cls == RegClass::FPR ? MOpcode::FMovRR
                                                                         : MOpcode::MovRR,
                                               {copy.dstReg, copy.srcReg}});
                }
            }
            predBlock.instrs.swap(rewritten);
        }

        std::vector<bool> removeLoadInstr(block.instrs.size(), false);
        for (const auto &load : loads)
            removeLoadInstr[load.instrIndex] = true;

        std::vector<MInstr> rewritten;
        rewritten.reserve(block.instrs.size());
        for (std::size_t ii = 0; ii < block.instrs.size(); ++ii) {
            if (removeLoadInstr[ii])
                continue;
            rewritten.push_back(block.instrs[ii]);
        }
        block.instrs.swap(rewritten);
        stats.deadInstructionsRemoved +=
            static_cast<int>(std::count(removeLoadInstr.begin(), removeLoadInstr.end(), true));
        changed = true;
    }

    return changed;
}

} // namespace

PeepholeStats runPeephole(MFunction &fn) {
    PeepholeStats stats;

    // Pass 0: Reorder blocks for better code layout
    stats.blocksReordered = static_cast<int>(ph::reorderBlocks(fn));

    // Pass 0.5: Hoist loop-invariant MovRI out of loop bodies.
    // LoopOpt now rejects merge-like headers, non-preheader entries, and uses
    // that can observe the value before a dominating definition inside the loop.
    stats.loopConstsHoisted = static_cast<int>(ph::hoistLoopConstants(fn));

    // Pass 0.7: Cross-block dead spill-store elimination.
    // Run after the local rewrites below so merged loads/stores are visible,
    // but before the later block-pair forwarding stage mutates cross-block uses.

    // (Pass 0.6 - loop phi spill elimination - runs after Pass 4.8 below)

    for (auto &block : fn.blocks) {
        auto &instrs = block.instrs;
        if (instrs.empty())
            continue;

        // Pass 0.9: Division/remainder strength reduction (multi-instruction patterns).
        // This must run BEFORE Pass 1's single-instruction strength reduction,
        // because Pass 1 converts UDIV->LSR which would break the UDIV+MSUB
        // remainder pattern. Remainder fusion must see the original UDIV/SDIV.
        {
            ph::RegConstMap divConsts;
            for (std::size_t i = 0; i < instrs.size(); ++i)
                ph::updateKnownConsts(instrs[i], divConsts);

            // First pass: try remainder fusion (UDIV/SDIV + MSUB -> AND/shift sequence)
            for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
                if (ph::tryRemainderFusion(instrs, i, divConsts, stats)) {
                    // Rebuild constant map after modification
                    divConsts.clear();
                    for (std::size_t j = 0; j < instrs.size(); ++j)
                        ph::updateKnownConsts(instrs[j], divConsts);
                    if (i > 0)
                        --i;
                }
            }

            // Second pass: try standalone SDIV strength reduction
            // (only for SDivRRR not already consumed by remainder fusion)
            divConsts.clear();
            for (std::size_t i = 0; i < instrs.size(); ++i)
                ph::updateKnownConsts(instrs[i], divConsts);

            for (std::size_t i = 0; i < instrs.size(); ++i) {
                if (instrs[i].opc == MOpcode::SDivRRR) {
                    if (ph::trySDivStrengthReduction(instrs, i, divConsts, stats)) {
                        // Rebuild constant map after modification
                        divConsts.clear();
                        for (std::size_t j = 0; j < instrs.size(); ++j)
                            ph::updateKnownConsts(instrs[j], divConsts);
                        // Don't decrement i -- the expansion replaces the current
                        // index and we should continue scanning from the next
                        // unprocessed instruction.
                    }
                }
            }
        }

        // Pass 1: Build register constant map and apply rewrites
        ph::RegConstMap knownConsts;
        for (auto &instr : instrs) {
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
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (ph::tryCbzCbnzFusion(instrs, i, stats)) {
                if (i > 0)
                    --i;
            }
        }

        // Pass 1.65: Cset+cbnz/cbz -> b.cond fusion
        for (std::size_t i = 0; i < instrs.size(); ++i) {
            if (ph::tryCsetBranchFusion(instrs, i, stats)) {
                if (i > 0)
                    --i;
            }
        }

        // Pass 1.7: MADD fusion (mul + add -> madd)
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (ph::tryMaddFusion(instrs, i, stats)) {
                if (i > 0)
                    --i;
            }
        }

        // Pass 1.8: LDP/STP merging (consecutive ldr/str with adjacent offsets)
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (ph::tryLdpStpMerge(instrs, i, stats)) {
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
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (!ph::tryFoldImmThenMove(instrs, i, stats))
                (void)ph::tryFoldConsecutiveMoves(instrs, i, stats);
        }

        // Pass 3: Mark identity moves for removal
        std::vector<bool> toRemove(instrs.size(), false);

        for (std::size_t i = 0; i < instrs.size(); ++i) {
            if (ph::isIdentityMovRR(instrs[i])) {
                toRemove[i] = true;
                ++stats.identityMovesRemoved;
            } else if (ph::isIdentityFMovRR(instrs[i])) {
                toRemove[i] = true;
                ++stats.identityFMovesRemoved;
            }
        }

        // Pass 4: Remove marked instructions
        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool v) { return v; })) {
            ph::removeMarkedInstructions(instrs, toRemove);
        }

        // Pass 4.5: Dead code elimination - remove instructions with unused results
        ph::removeDeadInstructions(instrs, stats);

        // Pass 4.6: Dead flag-setter elimination -- must run AFTER general DCE
        // so that dead Cset/Csel instructions (which read flags) are removed
        // first, exposing flag-setters whose results are truly unused.
        ph::removeDeadFlagSetters(instrs, stats);
    }

    ph::eliminateDeadFpStoresCrossBlock(fn, stats);

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
        for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
            for (const auto &mi : fn.blocks[bi].instrs) {
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

        for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi) {
            auto &predInstrs = fn.blocks[bi].instrs;
            auto &succBlock = fn.blocks[bi + 1];
            auto &succInstrs = succBlock.instrs;

            if (predInstrs.empty() || succInstrs.empty())
                continue;

            // Only forward to blocks with exactly one predecessor
            auto pcIt = predCount.find(succBlock.name);
            if (pcIt == predCount.end() || pcIt->second != 1)
                continue;

            // Verify the layout predecessor actually reaches the successor.
            // If block A ends with an unconditional branch to a DIFFERENT block,
            // it does NOT fall through to the layout successor.
            {
                bool reachesSucc = false;
                for (const auto &mi : predInstrs) {
                    // Unconditional branch to successor
                    if (mi.opc == MOpcode::Br && !mi.ops.empty() &&
                        mi.ops[0].kind == MOperand::Kind::Label &&
                        mi.ops[0].label == succBlock.name) {
                        reachesSucc = true;
                        break;
                    }
                    // Conditional branch to successor (fallthrough also possible)
                    if ((mi.opc == MOpcode::BCond || mi.opc == MOpcode::Cbz ||
                         mi.opc == MOpcode::Cbnz) &&
                        mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label &&
                        mi.ops[1].label == succBlock.name) {
                        reachesSucc = true;
                        break;
                    }
                }
                // Also check fallthrough: if last instruction is NOT an
                // unconditional branch or ret, execution falls through.
                if (!reachesSucc && !predInstrs.empty()) {
                    const auto &last = predInstrs.back();
                    if (last.opc != MOpcode::Br && last.opc != MOpcode::Ret)
                        reachesSucc = true;
                }
                if (!reachesSucc)
                    continue;
            }

            // Collect stores at the end of the predecessor block.
            struct StoreInfo {
                std::size_t idx;
                MOperand srcReg;
            };

            std::unordered_map<int64_t, StoreInfo> endStores;

            for (std::size_t i = predInstrs.size(); i-- > 0;) {
                const auto &instr = predInstrs[i];

                // Skip terminators
                if (instr.opc == MOpcode::Br || instr.opc == MOpcode::BCond ||
                    instr.opc == MOpcode::Ret || instr.opc == MOpcode::Cbz ||
                    instr.opc == MOpcode::Cbnz)
                    continue;

                // Record FP-relative stores
                if (instr.opc == MOpcode::StrRegFpImm && instr.ops.size() >= 2 &&
                    ph::isPhysReg(instr.ops[0]) && instr.ops[1].kind == MOperand::Kind::Imm) {
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

            struct PrefixLoad {
                std::size_t idx;
                MInstr instr;
            };
            struct ForwardPair {
                std::size_t idx;
                MOperand dstReg;
                MOperand srcReg;
            };

            std::vector<PrefixLoad> prefixLoads;
            for (std::size_t j = 0; j < succInstrs.size(); ++j) {
                const auto &instr = succInstrs[j];
                if (instr.opc != MOpcode::LdrRegFpImm)
                    break;
                if (instr.ops.size() < 2 || !ph::isPhysReg(instr.ops[0]) ||
                    instr.ops[1].kind != MOperand::Kind::Imm)
                    break;
                prefixLoads.push_back({j, instr});
            }
            if (prefixLoads.empty())
                continue;

            std::vector<ForwardPair> pending;
            std::unordered_set<std::size_t> forwardedIdx;
            pending.reserve(prefixLoads.size());
            for (const auto &load : prefixLoads) {
                const int64_t off = load.instr.ops[1].imm;
                auto it = endStores.find(off);
                if (it == endStores.end())
                    continue;
                pending.push_back({load.idx, load.instr.ops[0], it->second.srcReg});
            }
            if (pending.empty())
                continue;

            std::vector<ForwardPair> ordered;
            ordered.reserve(pending.size());
            while (!pending.empty()) {
                auto readyIt = pending.end();
                for (auto it = pending.begin(); it != pending.end(); ++it) {
                    const auto dstPhys = it->dstReg.reg.idOrPhys;
                    bool dstUsedAsSource = false;
                    for (auto jt = pending.begin(); jt != pending.end(); ++jt) {
                        if (it == jt)
                            continue;
                        if (jt->srcReg.reg.idOrPhys == dstPhys) {
                            dstUsedAsSource = true;
                            break;
                        }
                    }
                    if (!dstUsedAsSource) {
                        readyIt = it;
                        break;
                    }
                }
                if (readyIt == pending.end())
                    break; // Remaining copies form a cycle; keep their loads.
                forwardedIdx.insert(readyIt->idx);
                ordered.push_back(*readyIt);
                pending.erase(readyIt);
            }

            if (ordered.empty())
                continue;

            std::vector<MInstr> newPrefix;
            newPrefix.reserve(prefixLoads.size());
            for (const auto &pair : ordered) {
                if (pair.dstReg.reg.idOrPhys == pair.srcReg.reg.idOrPhys)
                    continue;
                newPrefix.push_back(MInstr{MOpcode::MovRR, {pair.dstReg, pair.srcReg}});
                ++stats.deadInstructionsRemoved;
            }
            for (const auto &load : prefixLoads) {
                if (forwardedIdx.count(load.idx))
                    continue;
                newPrefix.push_back(load.instr);
            }

            succInstrs.erase(succInstrs.begin(),
                             succInstrs.begin() +
                                 static_cast<std::ptrdiff_t>(prefixLoads.back().idx + 1));
            succInstrs.insert(succInstrs.begin(), newPrefix.begin(), newPrefix.end());
        }
    }

    // Pass 4.86: Forward single-predecessor phi-entry loads from predecessor
    // edge stores when the edge is acyclic and the source register survives to
    // the successor. This collapses direct join reloads without touching
    // loop-carried back-edges.
    if (forwardSinglePredPhiLoads(fn, stats))
        ph::eliminateDeadFpStoresCrossBlock(fn, stats);

    // Pass 4.88: Coalesce multi-predecessor join-entry phi loads into
    // predecessor register moves when every incoming edge already materializes
    // the values in physical registers before branching to the join. This cuts
    // stack round-trips that remain after the single-predecessor forwarding pass.
    if (coalesceJoinPhiLoads(fn, stats))
        ph::eliminateDeadFpStoresCrossBlock(fn, stats);

    // (Pass 4.85 moved to Pass 0.7 — runs before per-block loop above)

    // Pass 4.9: Eliminate redundant phi-slot spill/reload cycles in loop back-edges.
    // Must run AFTER Pass 4.8 (cross-block store-load forwarding) because that pass
    // may convert phi loads to register movs, changing the header's instruction mix.
    // Running after 4.8 ensures we see the final form of the header instructions.
    stats.loopConstsHoisted += static_cast<int>(ph::eliminateLoopPhiSpills(fn));

    // Pass 4.95: Re-run cross-block dead spill-store elimination after forwarding
    // and loop phi cleanup. Pass 4.8 can replace the only remaining reload from a
    // phi slot with a register move, which leaves the predecessor spill dead only
    // after the earlier Pass 0.7 has already run.
    ph::eliminateDeadFpStoresCrossBlock(fn, stats);

    // Pass 5: Branch inversion and branch-to-next removal.
    // This must be done after per-block passes since it looks at adjacent blocks.
    for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi) {
        auto &block = fn.blocks[bi];
        const auto &nextBlock = fn.blocks[bi + 1];

        if (block.instrs.empty())
            continue;

        // Branch inversion: b.cond .Ltarget; b .Lfallthrough
        // when .Ltarget == next block -> b.!cond .Lfallthrough (remove b .Lfallthrough)
        if (block.instrs.size() >= 2) {
            auto &secondLast = block.instrs[block.instrs.size() - 2];
            auto &last = block.instrs[block.instrs.size() - 1];

            if (secondLast.opc == MOpcode::BCond && secondLast.ops.size() == 2 &&
                secondLast.ops[0].kind == MOperand::Kind::Cond &&
                secondLast.ops[1].kind == MOperand::Kind::Label && last.opc == MOpcode::Br &&
                last.ops.size() == 1 && last.ops[0].kind == MOperand::Kind::Label) {
                if (secondLast.ops[1].label == nextBlock.name) {
                    const char *inv = ph::invertCondition(secondLast.ops[0].cond);
                    if (inv) {
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
        if (ph::isBranchTo(lastInstr, nextBlock.name)) {
            block.instrs.pop_back();
            ++stats.branchesToNextRemoved;
        }
    }

    return stats;
}

void pruneUnusedCalleeSaved(MFunction &fn) {
    // Build a set of all physical registers actually referenced in the MIR.
    std::unordered_set<uint16_t> usedRegs;
    for (const auto &bb : fn.blocks) {
        for (const auto &mi : bb.instrs) {
            for (const auto &op : mi.ops) {
                if (op.kind == MOperand::Kind::Reg && op.reg.isPhys)
                    usedRegs.insert(op.reg.idOrPhys);
            }
        }
    }

    // Prune savedGPRs: remove any callee-saved register not referenced.
    fn.savedGPRs.erase(std::remove_if(fn.savedGPRs.begin(),
                                      fn.savedGPRs.end(),
                                      [&](PhysReg r) {
                                          return usedRegs.find(static_cast<uint16_t>(r)) ==
                                                 usedRegs.end();
                                      }),
                       fn.savedGPRs.end());

    // Prune savedFPRs: same logic.
    fn.savedFPRs.erase(std::remove_if(fn.savedFPRs.begin(),
                                      fn.savedFPRs.end(),
                                      [&](PhysReg r) {
                                          return usedRegs.find(static_cast<uint16_t>(r)) ==
                                                 usedRegs.end();
                                      }),
                       fn.savedFPRs.end());
}

} // namespace viper::codegen::aarch64
