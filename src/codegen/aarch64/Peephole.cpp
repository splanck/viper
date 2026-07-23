//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/Peephole.cpp
// Purpose: Driver for conservative peephole optimizations over AArch64 MIR.
//          Orchestrates modular sub-passes and implements cross-block phi-join
//          load forwarding/coalescing using a dominator-based analysis.
// Key invariants:
//   - All rewrites preserve instruction semantics and ordering.
//   - Must be called after register allocation (physical registers required).
//   - Join-load coalescing skips loop headers whose natural loop contains calls.
// Ownership/Lifetime:
//   - Mutates MFunction in place; borrows fn only during the call.
// Links: codegen/aarch64/Peephole.hpp,
//        codegen/aarch64/peephole/ (sub-pass implementations)

#include "Peephole.hpp"

#include "Noreturn.hpp"
#include "peephole/BranchOpt.hpp"
#include "peephole/CopyPropDCE.hpp"
#include "peephole/Dominators.hpp"
#include "peephole/IdentityElim.hpp"
#include "peephole/LoopOpt.hpp"
#include "peephole/MemoryOpt.hpp"
#include "peephole/PeepholeCommon.hpp"
#include "peephole/StrengthReduce.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace zanna::codegen::aarch64 {

// Import sub-pass functions into local scope for concise call sites.
namespace ph = peephole;

namespace {

/// @brief Descriptor for an FP-relative load at the start of a join block.
struct JoinLoad {
    std::size_t instrIndex{0};   ///< Index of the load instruction in the block.
    int64_t offset{0};           ///< FP-relative byte offset loaded from.
    MOperand dstReg{};           ///< Destination register operand.
    RegClass cls{RegClass::GPR}; ///< GPR or FPR.
};

/// @brief Descriptor for an FP-relative store at the end of a predecessor block.
struct JoinStore {
    std::size_t instrIndex{0};   ///< Index of the store instruction in the predecessor.
    int64_t offset{0};           ///< FP-relative byte offset stored to.
    MOperand srcReg{};           ///< Source register operand.
    RegClass cls{RegClass::GPR}; ///< GPR or FPR.
};

/// @brief A single register-to-register copy to be inserted on a pred→join edge.
struct JoinCopy {
    MOperand srcReg{};           ///< Source (value stored in predecessor).
    MOperand dstReg{};           ///< Destination (value loaded in join block).
    RegClass cls{RegClass::GPR}; ///< GPR or FPR.
};

/// @brief Build a 32-bit key encoding register class and physical ID for set lookups.
static std::uint32_t regKey(const MOperand &op) {
    return (static_cast<std::uint32_t>(op.reg.cls) << 16) |
           static_cast<std::uint32_t>(op.reg.idOrPhys);
}

/// @brief Record a physical register that a post-RA rewrite makes live across
///        @p block's exit.
/// @details Register allocation publishes this sorted metadata before the
///          peephole pipeline runs. Cross-block rewrites that replace stack
///          transfers with register transfers must extend it so later local
///          rewrites do not erase the newly live value.
static void markCarriedExitReg(MBasicBlock &block, const MOperand &reg) {
    if (!ph::isPhysReg(reg))
        return;
    const uint16_t phys = reg.reg.idOrPhys;
    const auto insertion =
        std::lower_bound(block.carriedExitRegs.begin(), block.carriedExitRegs.end(), phys);
    if (insertion == block.carriedExitRegs.end() || *insertion != phys)
        block.carriedExitRegs.insert(insertion, phys);
}

/// @brief Return true if block @p blockIndex can fall through to @p succName without a branch.
static bool blockFallsThroughTo(const MFunction &fn,
                                std::size_t blockIndex,
                                const std::string &succName) {
    if (blockIndex + 1 >= fn.blocks.size())
        return false;
    if (fn.blocks[blockIndex + 1].name != succName)
        return false;
    const auto &instrs = fn.blocks[blockIndex].instrs;
    if (instrs.empty())
        return true;
    const auto &last = instrs.back();
    if (isNoReturnCall(last))
        return false;
    return last.opc != MOpcode::Br && last.opc != MOpcode::Ret;
}

/// @brief Build a map from block name to the list of predecessor block indices.
/// @details Accounts for fall-through edges as well as explicit branch targets.
static std::unordered_map<std::string, std::vector<std::size_t>> buildPredecessorMap(
    const MFunction &fn) {
    std::unordered_map<std::string, std::vector<std::size_t>> preds;
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        const auto &block = fn.blocks[bi];
        const auto addPred = [&](const std::string &label) {
            auto &list = preds[label];
            if (std::find(list.begin(), list.end(), bi) == list.end())
                list.push_back(bi);
        };
        const auto addFallthrough = [&]() {
            if (bi + 1 < fn.blocks.size())
                addPred(fn.blocks[bi + 1].name);
        };
        const auto isConditionalBranch = [](const MInstr &instr) {
            return instr.opc == MOpcode::BCond || instr.opc == MOpcode::Cbz ||
                   instr.opc == MOpcode::Cbnz || instr.opc == MOpcode::Tbz ||
                   instr.opc == MOpcode::Tbnz;
        };
        if (block.instrs.empty()) {
            addFallthrough();
            continue;
        }

        for (const auto &instr : block.instrs) {
            if (isConditionalBranch(instr) && instr.ops.size() >= 2 &&
                instr.ops[1].kind == MOperand::Kind::Label)
                addPred(instr.ops[1].label);
        }

        const auto &last = block.instrs.back();
        if (last.opc == MOpcode::Br && !last.ops.empty() &&
            last.ops[0].kind == MOperand::Kind::Label)
            addPred(last.ops[0].label);
        else if (last.opc != MOpcode::Ret && !isNoReturnCall(last))
            addFallthrough();
    }
    return preds;
}

/// @brief Return true if @p predIndex has a direct (non-conditional) edge to @p succName.
static bool isDirectPredEdgeTo(const MFunction &fn,
                               std::size_t predIndex,
                               const std::string &succName) {
    const auto &instrs = fn.blocks[predIndex].instrs;
    if (instrs.empty())
        return blockFallsThroughTo(fn, predIndex, succName);
    const auto &last = instrs.back();
    if (last.opc == MOpcode::Br && !last.ops.empty() && last.ops[0].kind == MOperand::Kind::Label)
        return last.ops[0].label == succName;
    return blockFallsThroughTo(fn, predIndex, succName);
}

/// @brief Return true if copies can safely be inserted at the end of the pred→succ edge.
/// @details Unconditional-branch and fall-through edges permit insertion;
///          conditional branches, returns, and CBZ/CBNZ edges do not.
static bool canInsertJoinCopiesOnPredEdge(const MFunction &fn,
                                          std::size_t predIndex,
                                          const std::string &succName) {
    const auto &instrs = fn.blocks[predIndex].instrs;
    if (instrs.empty())
        return blockFallsThroughTo(fn, predIndex, succName);
    const auto &last = instrs.back();
    if (last.opc == MOpcode::Br && !last.ops.empty() && last.ops[0].kind == MOperand::Kind::Label)
        return last.ops[0].label == succName;
    if (last.opc == MOpcode::Ret || isNoReturnCall(last))
        return false;
    return blockFallsThroughTo(fn, predIndex, succName);
}

/// @brief Collect the leading FP-relative LDR/LDP instructions in @p block into @p loads.
/// @return True if at least one load was found; stops at the first non-load instruction.
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
                loads.push_back(
                    {i,
                     instr.ops[1].imm,
                     instr.ops[0],
                     instr.opc == MOpcode::LdrFprFpImm ? RegClass::FPR : RegClass::GPR});
                continue;
            case MOpcode::LdpRegFpImm:
            case MOpcode::LdpFprFpImm:
                if (instr.ops.size() < 3 || !ph::isPhysReg(instr.ops[0]) ||
                    !ph::isPhysReg(instr.ops[1]) || instr.ops[2].kind != MOperand::Kind::Imm)
                    return !loads.empty();
                loads.push_back(
                    {i,
                     instr.ops[2].imm,
                     instr.ops[0],
                     instr.opc == MOpcode::LdpFprFpImm ? RegClass::FPR : RegClass::GPR});
                loads.push_back(
                    {i,
                     instr.ops[2].imm + 8,
                     instr.ops[1],
                     instr.opc == MOpcode::LdpFprFpImm ? RegClass::FPR : RegClass::GPR});
                continue;
            default:
                return !loads.empty();
        }
    }
    return !loads.empty();
}

/// @brief Collect trailing FP-relative STR/STP instructions in the predecessor block.
/// @details Scans backward from the block's last non-branch instruction; stops at any
///          instruction that is not a load, move, or store. Builds a map from FP offset
///          to JoinStore and a set of the store instruction indices.
/// @return True if at least one store was found.
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
        if (last.opc == MOpcode::Br && !last.ops.empty() &&
            last.ops[0].kind == MOperand::Kind::Label && last.ops[0].label == succName)
            scanEnd = instrs.size() - 1;
    }

    std::unordered_set<std::uint32_t> clobbered;
    std::unordered_set<int64_t> blockedOffsets;
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
            if (stores.find(off) == stores.end() && !blockedOffsets.count(off)) {
                if (clobbered.count(key)) {
                    blockedOffsets.insert(off);
                    continue;
                }
                stores.emplace(
                    off,
                    JoinStore{i,
                              off,
                              instr.ops[0],
                              instr.opc == MOpcode::StrFprFpImm ? RegClass::FPR : RegClass::GPR});
                storeInstrs.insert(i);
            }
            continue;
        }

        if ((instr.opc == MOpcode::StpRegFpImm || instr.opc == MOpcode::StpFprFpImm) &&
            instr.ops.size() >= 3 && ph::isPhysReg(instr.ops[0]) && ph::isPhysReg(instr.ops[1]) &&
            instr.ops[2].kind == MOperand::Kind::Imm) {
            const RegClass cls = instr.opc == MOpcode::StpFprFpImm ? RegClass::FPR : RegClass::GPR;
            const int64_t baseOff = instr.ops[2].imm;
            const std::array<std::pair<int64_t, MOperand>, 2> pairStores = {
                {{baseOff, instr.ops[0]}, {baseOff + 8, instr.ops[1]}}};
            for (const auto &[off, src] : pairStores) {
                if (stores.count(off) || blockedOffsets.count(off))
                    continue;
                if (clobbered.count(regKey(src))) {
                    blockedOffsets.insert(off);
                    continue;
                }
                stores.emplace(off, JoinStore{i, off, src, cls});
                storeInstrs.insert(i);
            }
            continue;
        }

        break;
    }

    return !stores.empty();
}

/// @brief Topologically order @p copies so that no copy overwrites a source before it is used.
/// @return True if a safe order was found; false if a dependency cycle prevents ordering.
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

/// @brief Remove selected join-prefix loads, splitting LDP into LDR when only one half remains.
static void removeForwardedJoinLoads(MBasicBlock &block,
                                     const std::vector<JoinLoad> &loads,
                                     const std::unordered_set<std::size_t> &forwardedLoads) {
    std::unordered_map<std::size_t, std::vector<std::size_t>> loadsByInstr;
    for (std::size_t loadIndex = 0; loadIndex < loads.size(); ++loadIndex)
        loadsByInstr[loads[loadIndex].instrIndex].push_back(loadIndex);

    std::vector<MInstr> rewritten;
    rewritten.reserve(block.instrs.size());
    for (std::size_t instrIndex = 0; instrIndex < block.instrs.size(); ++instrIndex) {
        const auto loadIt = loadsByInstr.find(instrIndex);
        if (loadIt == loadsByInstr.end()) {
            rewritten.push_back(block.instrs[instrIndex]);
            continue;
        }

        const auto &instr = block.instrs[instrIndex];
        const auto &indices = loadIt->second;
        if (instr.opc == MOpcode::LdrRegFpImm || instr.opc == MOpcode::LdrFprFpImm) {
            if (indices.empty() || forwardedLoads.count(indices.front()) == 0)
                rewritten.push_back(instr);
            continue;
        }

        if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
            indices.size() == 2 && instr.ops.size() >= 3 &&
            instr.ops[2].kind == MOperand::Kind::Imm) {
            const bool firstForwarded = forwardedLoads.count(indices[0]) != 0;
            const bool secondForwarded = forwardedLoads.count(indices[1]) != 0;
            if (firstForwarded && secondForwarded)
                continue;
            if (!firstForwarded && !secondForwarded) {
                rewritten.push_back(instr);
                continue;
            }

            const bool keepSecond = firstForwarded;
            const MOpcode loadOpcode =
                instr.opc == MOpcode::LdpFprFpImm ? MOpcode::LdrFprFpImm : MOpcode::LdrRegFpImm;
            const MOperand dst = instr.ops[keepSecond ? 1 : 0];
            const int64_t offset = instr.ops[2].imm + (keepSecond ? 8 : 0);
            rewritten.push_back(
                MInstr{loadOpcode, {dst, MOperand::immOp(static_cast<long long>(offset))}});
            continue;
        }

        rewritten.push_back(instr);
    }
    block.instrs.swap(rewritten);
}

/// @brief Add all registers defined (clobbered) by @p instr to @p clobbered.
/// @details Calls clobber all caller-saved GPRs and FPRs; LDP clobbers both result regs.
[[maybe_unused]] static void markInstructionDefs(const MInstr &instr,
                                                 std::unordered_set<std::uint32_t> &clobbered) {
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

/// @brief Collect all block indices in the natural loop with @p headerIndex and @p latchIndex.
/// @details Uses a backward reachability worklist from the latch; the header bounds the search.
static std::unordered_set<std::size_t> collectNaturalLoopBlocks(
    const MFunction &fn,
    const std::unordered_map<std::string, std::vector<std::size_t>> &preds,
    std::size_t headerIndex,
    std::size_t latchIndex) {
    std::unordered_set<std::size_t> loopBlocks;
    std::vector<std::size_t> worklist;
    loopBlocks.insert(headerIndex);
    worklist.push_back(latchIndex);

    while (!worklist.empty()) {
        const std::size_t blockIndex = worklist.back();
        worklist.pop_back();
        if (blockIndex >= fn.blocks.size())
            continue;
        if (!loopBlocks.insert(blockIndex).second)
            continue;

        auto predIt = preds.find(fn.blocks[blockIndex].name);
        if (predIt == preds.end())
            continue;
        for (std::size_t predIndex : predIt->second) {
            if (loopBlocks.count(predIndex) == 0)
                worklist.push_back(predIndex);
        }
    }

    return loopBlocks;
}

/// @brief Return true if any block in @p blocks contains a Bl or Blr instruction.
static bool blocksContainCall(const MFunction &fn, const std::unordered_set<std::size_t> &blocks) {
    for (std::size_t blockIndex : blocks) {
        if (blockIndex >= fn.blocks.size())
            return true;
        for (const auto &instr : fn.blocks[blockIndex].instrs) {
            if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr)
                return true;
        }
    }
    return false;
}

/// @brief Adapt the name-keyed predecessor map produced by buildPredecessorMap into
///        the dense index-keyed form consumed by ph::computeDominators.
static std::vector<std::vector<std::size_t>> indexedPreds(
    const MFunction &fn, const std::unordered_map<std::string, std::vector<std::size_t>> &preds) {
    std::vector<std::vector<std::size_t>> result(fn.blocks.size());
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        auto it = preds.find(fn.blocks[i].name);
        if (it != preds.end())
            result[i] = it->second;
    }
    return result;
}

/// @brief Replace leading FP-relative loads in single-predecessor join blocks with copies.
/// @details When all loads match trailing stores in the unique predecessor, eliminates
///          the memory traffic by inserting MOV/FMOV copies on the pred→join edge.
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

        // Removing the successor loads makes every stored source register a
        // live-in to this block, including identity copies omitted by the move
        // ordering helper.
        for (const auto &copy : copies)
            markCarriedExitReg(fn.blocks[predIndex], copy.srcReg);

        std::vector<bool> removeLoadInstr(block.instrs.size(), false);
        for (const auto &load : loads)
            removeLoadInstr[load.instrIndex] = true;

        std::vector<MInstr> rewritten;
        rewritten.reserve(block.instrs.size() -
                          std::count(removeLoadInstr.begin(), removeLoadInstr.end(), true) +
                          ordered.size());
        bool insertedMoves = false;
        for (std::size_t ii = 0; ii < block.instrs.size(); ++ii) {
            if (removeLoadInstr[ii]) {
                if (!insertedMoves) {
                    for (const auto &copy : ordered) {
                        rewritten.push_back(
                            MInstr{copy.cls == RegClass::FPR ? MOpcode::FMovRR : MOpcode::MovRR,
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

/// @brief Coalesce leading phi-join loads across all predecessors of a multi-pred block.
/// @details For each predecessor, inserts MOV/FMOV copies before the branch so the
///          join block can simply read the value from its destination register rather
///          than reloading from the stack. Skips loop headers that contain calls.
static bool coalesceJoinPhiLoads(MFunction &fn, PeepholeStats &stats) {
    bool changed = false;
    const auto preds = buildPredecessorMap(fn);
    const auto dominators = ph::computeDominators(fn.blocks.size(), indexedPreds(fn, preds));

    for (std::size_t blockIndex = 0; blockIndex < fn.blocks.size(); ++blockIndex) {
        auto &block = fn.blocks[blockIndex];
        std::vector<JoinLoad> loads;
        if (!collectJoinPrefixLoads(block, loads))
            continue;

        auto predIt = preds.find(block.name);
        if (predIt == preds.end() || predIt->second.size() < 2)
            continue;

        // Loop headers are safe to coalesce only while their natural loop is
        // call-free. If the loop body calls out, the generic join rewrite can
        // keep loop-carried values only in physical registers across complex
        // call-heavy paths without proving every rewritten register live range.
        bool unsafeLoopHeader = false;
        for (std::size_t predIndex : predIt->second) {
            if (predIndex < blockIndex)
                continue;
            if (!dominators.dominates(blockIndex, predIndex))
                continue;
            const auto loopBlocks = collectNaturalLoopBlocks(fn, preds, blockIndex, predIndex);
            if (blocksContainCall(fn, loopBlocks)) {
                unsafeLoopHeader = true;
                break;
            }
        }
        if (unsafeLoopHeader)
            continue;

        struct PredStorePlan {
            std::size_t predIndex{0};                      ///< Predecessor block index.
            std::unordered_map<int64_t, JoinStore> stores; ///< Stores keyed by FP offset.
        };

        std::vector<PredStorePlan> predStorePlans;
        predStorePlans.reserve(predIt->second.size());
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
            predStorePlans.push_back({predIndex, std::move(stores)});
        }

        if (!allPredsConvertible || predStorePlans.empty())
            continue;

        std::vector<std::size_t> selectedLoads;
        selectedLoads.reserve(loads.size());
        for (std::size_t loadIndex = 0; loadIndex < loads.size(); ++loadIndex) {
            const auto &load = loads[loadIndex];
            bool availableOnEveryPred = true;
            for (const auto &plan : predStorePlans) {
                const auto storeIt = plan.stores.find(load.offset);
                if (storeIt == plan.stores.end() || storeIt->second.cls != load.cls) {
                    availableOnEveryPred = false;
                    break;
                }
            }
            if (availableOnEveryPred)
                selectedLoads.push_back(loadIndex);
        }

        if (selectedLoads.empty())
            continue;

        std::vector<std::vector<JoinCopy>> predCopies;
        predCopies.reserve(predStorePlans.size());
        const auto buildPredCopies = [&](const std::vector<std::size_t> &loadIndices) {
            predCopies.clear();
            predCopies.reserve(predStorePlans.size());
            if (loadIndices.empty())
                return false;
            for (const auto &plan : predStorePlans) {
                std::vector<JoinCopy> copies;
                copies.reserve(loadIndices.size());
                for (std::size_t loadIndex : loadIndices) {
                    const auto &load = loads[loadIndex];
                    auto storeIt = plan.stores.find(load.offset);
                    if (storeIt == plan.stores.end() || storeIt->second.cls != load.cls)
                        return false;
                    copies.push_back({storeIt->second.srcReg, load.dstReg, load.cls});
                }

                std::vector<JoinCopy> ordered;
                if (!orderJoinCopies(copies, ordered))
                    return false;
                predCopies.push_back(std::move(ordered));
            }
            return true;
        };

        while (!selectedLoads.empty() && !buildPredCopies(selectedLoads))
            selectedLoads.pop_back();

        if (selectedLoads.empty() || predCopies.empty())
            continue;

        for (std::size_t pi = 0; pi < predStorePlans.size(); ++pi) {
            auto &predBlock = fn.blocks[predStorePlans[pi].predIndex];
            const bool branchesDirectly =
                !predBlock.instrs.empty() && predBlock.instrs.back().opc == MOpcode::Br &&
                predBlock.instrs.back().ops.size() == 1 &&
                predBlock.instrs.back().ops[0].kind == MOperand::Kind::Label &&
                predBlock.instrs.back().ops[0].label == block.name;
            const std::size_t insertAt =
                branchesDirectly ? predBlock.instrs.size() - 1 : predBlock.instrs.size();

            std::vector<MInstr> rewritten;
            rewritten.reserve(predBlock.instrs.size() + predCopies[pi].size());
            for (std::size_t ii = 0; ii < predBlock.instrs.size(); ++ii) {
                if (ii == insertAt) {
                    for (const auto &copy : predCopies[pi]) {
                        rewritten.push_back(
                            MInstr{copy.cls == RegClass::FPR ? MOpcode::FMovRR : MOpcode::MovRR,
                                   {copy.dstReg, copy.srcReg}});
                    }
                }
                rewritten.push_back(predBlock.instrs[ii]);
            }
            if (insertAt == predBlock.instrs.size()) {
                for (const auto &copy : predCopies[pi]) {
                    rewritten.push_back(
                        MInstr{copy.cls == RegClass::FPR ? MOpcode::FMovRR : MOpcode::MovRR,
                               {copy.dstReg, copy.srcReg}});
                }
            }
            predBlock.instrs.swap(rewritten);

            // The join now consumes each load destination directly from this
            // edge. Preserve the copy (or an identity-carried value) through
            // post-schedule block-local cleanup.
            for (std::size_t loadIndex : selectedLoads)
                markCarriedExitReg(predBlock, loads[loadIndex].dstReg);
        }

        const std::unordered_set<std::size_t> forwardedLoadSet(selectedLoads.begin(),
                                                               selectedLoads.end());
        removeForwardedJoinLoads(block, loads, forwardedLoadSet);
        stats.deadInstructionsRemoved += static_cast<int>(forwardedLoadSet.size());
        changed = true;
    }

    return changed;
}

/// @brief Pass 4.8 — forward store/load pairs between layout-adjacent blocks.
/// @details When block A ends with `str Rx, [fp, #off]` and its single successor
///          B begins with `ldr Ry, [fp, #off]`, replace the load with `mov Ry, Rx`.
///          Only triggers when B has exactly one predecessor (A); multi-predecessor
///          joins are handled by other passes that prove all incoming paths agree.
static void forwardLayoutSuccessorStoreLoad(MFunction &fn, PeepholeStats &stats) {
    const auto preds = buildPredecessorMap(fn);

    for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi) {
        auto &predInstrs = fn.blocks[bi].instrs;
        auto &succBlock = fn.blocks[bi + 1];
        auto &succInstrs = succBlock.instrs;

        if (predInstrs.empty() || succInstrs.empty())
            continue;

        // Only forward to blocks with exactly one real predecessor — including
        // fallthrough edges — otherwise short-circuit boolean joins can be
        // miscompiled by forwarding only one incoming value.
        auto predIt = preds.find(succBlock.name);
        if (predIt == preds.end() || predIt->second.size() != 1 || predIt->second.front() != bi)
            continue;

        // Verify the layout predecessor actually reaches the successor; an
        // unconditional branch to a DIFFERENT block disqualifies the fallthrough.
        {
            bool reachesSucc = false;
            for (const auto &mi : predInstrs) {
                if (mi.opc == MOpcode::Br && !mi.ops.empty() &&
                    mi.ops[0].kind == MOperand::Kind::Label && mi.ops[0].label == succBlock.name) {
                    reachesSucc = true;
                    break;
                }
                if ((mi.opc == MOpcode::BCond || mi.opc == MOpcode::Cbz ||
                     mi.opc == MOpcode::Cbnz || mi.opc == MOpcode::Tbz ||
                     mi.opc == MOpcode::Tbnz) &&
                    mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label &&
                    mi.ops[1].label == succBlock.name) {
                    reachesSucc = true;
                    break;
                }
            }
            if (!reachesSucc && !predInstrs.empty()) {
                const auto &last = predInstrs.back();
                if (last.opc != MOpcode::Br && last.opc != MOpcode::Ret)
                    reachesSucc = true;
            }
            if (!reachesSucc)
                continue;
        }

        // Collect trailing FP-relative stores in the predecessor.
        struct StoreInfo {
            std::size_t idx{0}; ///< Store instruction index in the predecessor block.
            MOperand srcReg{};  ///< Stored source register.
        };

        std::unordered_map<int64_t, StoreInfo> endStores;

        for (std::size_t i = predInstrs.size(); i-- > 0;) {
            const auto &instr = predInstrs[i];

            if (instr.opc == MOpcode::Br || instr.opc == MOpcode::BCond ||
                instr.opc == MOpcode::Ret || instr.opc == MOpcode::Cbz ||
                instr.opc == MOpcode::Cbnz || instr.opc == MOpcode::Tbz ||
                instr.opc == MOpcode::Tbnz || instr.opc == MOpcode::JumpTable)
                continue;

            if (instr.opc == MOpcode::StrRegFpImm && instr.ops.size() >= 2 &&
                ph::isPhysReg(instr.ops[0]) && instr.ops[1].kind == MOperand::Kind::Imm) {
                const int64_t off = instr.ops[1].imm;
                endStores.try_emplace(off, StoreInfo{i, instr.ops[0]});
                continue;
            }

            break; // Non-store, non-terminator: stop scanning backward.
        }

        if (endStores.empty())
            continue;

        struct PrefixLoad {
            std::size_t idx{0}; ///< Load instruction index in the successor block.
            MInstr instr{};     ///< Load instruction to replace when forwarding succeeds.
        };

        struct ForwardPair {
            std::size_t idx{0}; ///< Successor load index to rewrite.
            MOperand dstReg{};  ///< Destination loaded register.
            MOperand srcReg{};  ///< Predecessor stored register.
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

        // Greedy topological ordering — a destination cannot be overwritten
        // before its value has been read elsewhere in the move set.
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

        // The replacement moves execute in the successor, so their source
        // registers are newly live across the predecessor's exit. This also
        // covers identity pairs, for which no explicit move is emitted.
        for (const auto &pair : ordered)
            markCarriedExitReg(fn.blocks[bi], pair.srcReg);

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

/// @brief Run the local per-block rewrite passes (formerly inline in `runPeephole`).
/// @details Encompasses passes 0.9 through 4.6 — division strength reduction,
///          constant-aware single-instruction rewrites, fusion (cbz, cset+branch,
///          madd, ldp/stp), store/load forwarding, identity-move removal, and
///          local DCE. Behaviour is unchanged from the original inline loop.
static void runPerBlockRewrites(MFunction &fn, PeepholeStats &stats, const TargetInfo *target) {
    for (auto &block : fn.blocks) {
        auto &instrs = block.instrs;
        if (instrs.empty())
            continue;

        // Pass 0.9: Division/remainder strength reduction (multi-instruction patterns).
        // Must run BEFORE Pass 1's single-instruction strength reduction, because
        // Pass 1 converts UDIV->LSR which would break the UDIV+MSUB remainder
        // pattern. Remainder fusion must see the original UDIV/SDIV.
        {
            bool changed = true;
            while (changed) {
                changed = false;
                ph::RegConstMap divConsts;
                for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
                    if (ph::tryRemainderFusion(
                            instrs, i, divConsts, stats, &block.carriedExitRegs)) {
                        changed = true;
                        break;
                    }
                    ph::updateKnownConsts(instrs[i], divConsts);
                }
            }

            changed = true;
            while (changed) {
                changed = false;
                ph::RegConstMap divConsts;
                for (std::size_t i = 0; i < instrs.size(); ++i) {
                    bool localChange = false;
                    if (instrs[i].opc == MOpcode::UDivRRR)
                        localChange = ph::tryUDivStrengthReduction(
                            instrs, i, divConsts, stats, &block.carriedExitRegs);
                    else if (instrs[i].opc == MOpcode::SDivRRR)
                        localChange = ph::trySDivStrengthReduction(
                            instrs, i, divConsts, stats, &block.carriedExitRegs);

                    if (localChange) {
                        changed = true;
                        break;
                    }
                    ph::updateKnownConsts(instrs[i], divConsts);
                }
            }
        }

        // Pass 1: Single-instruction constant-aware rewrites.
        ph::RegConstMap knownConsts;
        for (auto &instr : instrs) {
            ph::updateKnownConsts(instr, knownConsts);
            if (ph::tryCmpZeroToTst(instr, stats))
                continue;
            if (ph::tryArithmeticIdentity(instr, stats))
                continue;
            (void)ph::tryStrengthReduction(instr, knownConsts, stats);
            (void)ph::tryDivStrengthReduction(instr, knownConsts, stats);
            (void)ph::tryImmediateFolding(instr, knownConsts, stats);
        }

        // Pass 1.5: Copy propagation.
        ph::propagateCopies(instrs, stats);

        // Pass 1.6 / 1.65 / 1.7 / 1.8: instruction-level fusions.
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (ph::tryCbzCbnzFusion(instrs, i, stats) && i > 0)
                --i;
        }
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (ph::tryTbzTbnzFusion(instrs, i, stats, &block.carriedExitRegs) && i > 0)
                --i;
        }
        for (std::size_t i = 0; i < instrs.size(); ++i) {
            if (ph::tryCsetBranchFusion(instrs, i, stats, &block.carriedExitRegs) && i > 0)
                --i;
        }
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (ph::tryMaddFusion(instrs, i, stats) && i > 0)
                --i;
        }
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (ph::tryLdpStpMerge(instrs, i, stats) && i > 0)
                --i;
        }

        // Pass 1.85 / 1.9 / 1.97: local store/load shuffling.
        ph::eliminateDeadFpStores(instrs, stats);
        ph::forwardStoreLoads(instrs, stats);
        ph::foldComputeIntoTarget(instrs, stats);

        // Pass 2: Fold consecutive moves.
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (!ph::tryFoldImmThenMove(instrs, i, stats, &block.carriedExitRegs))
                (void)ph::tryFoldConsecutiveMoves(instrs, i, stats, &block.carriedExitRegs);
        }

        // Pass 3+4: Mark and remove identity moves.
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
        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool v) { return v; }))
            ph::removeMarkedInstructions(instrs, toRemove);

        // Pass 4.5: Local DCE. The modular pipeline runs the CFG-aware variant
        // post-block; direct unit tests (no target) keep the legacy per-block path.
        if (target == nullptr)
            ph::removeDeadInstructions(instrs, stats, &block.carriedExitRegs);

        // Pass 4.6: Dead flag-setter elimination AFTER DCE so dead readers of
        // flags are gone before we judge a flag-setter unused.
        ph::removeDeadFlagSetters(instrs, stats);
    }
}

/// @brief Apply branch inversion and branch-to-next removal across the function.
/// @details Shared by both `runPeephole` (final pass 5) and `runPostSchedulePeephole`:
///          previously copy-pasted in two places.
static void runBranchInversionAndCleanup(MFunction &fn, PeepholeStats &stats) {
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

        // Remove branches to the immediately following block.
        if (ph::isBranchTo(block.instrs.back(), nextBlock.name)) {
            block.instrs.pop_back();
            ++stats.branchesToNextRemoved;
        }
    }
}

} // namespace

PeepholeStats runPeephole(MFunction &fn, const TargetInfo *target) {
    PeepholeStats stats;

    // Pass 0: Reorder blocks for better code layout
    stats.blocksReordered = static_cast<int>(ph::reorderBlocks(fn));

    // Pass 0.5: Hoist loop-invariant MovRI out of loop bodies.
    // LoopOpt now rejects merge-like headers, non-preheader entries, and uses
    // that can observe the value before a dominating definition inside the loop.
    stats.loopConstsHoisted = static_cast<int>(ph::hoistLoopConstants(fn));

    // Passes 0.9 through 4.6: local per-block rewrites (division strength reduction,
    // constant-aware rewrites, fusions, identity removal, local DCE/flag DCE).
    // (Pass 0.6 — loop phi spill elimination — runs after Pass 4.8 below.)
    runPerBlockRewrites(fn, stats, target);

    if (target != nullptr) {
        ph::removeDeadInstructionsCFG(fn, stats, *target);
        for (auto &block : fn.blocks)
            ph::removeDeadFlagSetters(block.instrs, stats);
    }

    ph::eliminateDeadFpStoresCrossBlock(fn, stats);

    // Pass 4.8: Cross-block store-load forwarding for phi stores/loads.
    // Forwards single-predecessor join blocks; multi-predecessor joins handled
    // by passes 4.86 / 4.88 below.
    forwardLayoutSuccessorStoreLoad(fn, stats);

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
    for (int iter = 0; iter < 16; ++iter) {
        const auto eliminated = ph::eliminateLoopPhiSpills(fn);
        if (eliminated == 0)
            break;
        stats.loopConstsHoisted += static_cast<int>(eliminated);
        ph::eliminateDeadFpStoresCrossBlock(fn, stats);
    }

    // Pass 4.95: Re-run cross-block dead spill-store elimination after forwarding
    // and loop phi cleanup. Pass 4.8 can replace the only remaining reload from a
    // phi slot with a register move, which leaves the predecessor spill dead only
    // after the earlier Pass 0.7 has already run.
    ph::eliminateDeadFpStoresCrossBlock(fn, stats);

    // Pass 5: Branch inversion and branch-to-next removal.
    runBranchInversionAndCleanup(fn, stats);

    return stats;
}

PeepholeStats runPostSchedulePeephole(MFunction &fn, const TargetInfo *target) {
    PeepholeStats stats;

    for (auto &block : fn.blocks) {
        auto &instrs = block.instrs;
        if (instrs.empty())
            continue;

        ph::propagateCopies(instrs, stats);

        for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
            if (!ph::tryFoldImmThenMove(instrs, i, stats, &block.carriedExitRegs))
                (void)ph::tryFoldConsecutiveMoves(instrs, i, stats, &block.carriedExitRegs);
        }

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
        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool v) { return v; }))
            ph::removeMarkedInstructions(instrs, toRemove);

        if (target == nullptr)
            ph::removeDeadInstructions(instrs, stats, &block.carriedExitRegs);
        ph::removeDeadFlagSetters(instrs, stats);
    }

    runBranchInversionAndCleanup(fn, stats);

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

} // namespace zanna::codegen::aarch64
