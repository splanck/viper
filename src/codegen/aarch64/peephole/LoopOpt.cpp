//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/LoopOpt.cpp
// Purpose: Loop-invariant constant hoisting for the AArch64 peephole optimizer.
//
// Key invariants:
//   - Only hoists MovRI to callee-saved registers (x19-x28).
//   - The register must be defined only by MovRI with the same immediate value
//     throughout the loop body.
//
// Ownership/Lifetime:
//   - Operates on mutable MFunction owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "LoopOpt.hpp"

#include "PeepholeCommon.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64::peephole {
namespace {

/// @brief Check whether a physical register is callee-saved (x19-x28).
[[nodiscard]] bool isCalleeSavedGPR(uint32_t phys) noexcept {
    return phys >= static_cast<uint32_t>(PhysReg::X19) &&
           phys <= static_cast<uint32_t>(PhysReg::X28);
}

} // namespace

std::size_t hoistLoopConstants(MFunction &fn) {
    if (fn.blocks.size() < 3)
        return 0;

    // Build block-name -> block-index map.
    std::unordered_map<std::string, std::size_t> nameToIdx;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        nameToIdx[fn.blocks[i].name] = i;

    auto getBranchTarget = [](const MInstr &mi) -> std::string {
        if (mi.opc == MOpcode::Br && !mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Label)
            return mi.ops[0].label;
        if (mi.opc == MOpcode::BCond && mi.ops.size() >= 2 &&
            mi.ops[1].kind == MOperand::Kind::Label)
            return mi.ops[1].label;
        if ((mi.opc == MOpcode::Cbz || mi.opc == MOpcode::Cbnz) && mi.ops.size() >= 2 &&
            mi.ops[1].kind == MOperand::Kind::Label)
            return mi.ops[1].label;
        return {};
    };

    auto isNonDefOpc = [](MOpcode opc) -> bool {
        return opc == MOpcode::StrRegFpImm || opc == MOpcode::StrRegBaseImm ||
               opc == MOpcode::StrRegSpImm || opc == MOpcode::StrFprFpImm ||
               opc == MOpcode::StrFprBaseImm || opc == MOpcode::StrFprSpImm ||
               opc == MOpcode::StpRegFpImm || opc == MOpcode::StpFprFpImm ||
               opc == MOpcode::CmpRR || opc == MOpcode::CmpRI || opc == MOpcode::TstRR ||
               opc == MOpcode::FCmpRR || opc == MOpcode::Br || opc == MOpcode::BCond ||
               opc == MOpcode::Cbz || opc == MOpcode::Cbnz || opc == MOpcode::Ret ||
               opc == MOpcode::Bl || opc == MOpcode::Blr || opc == MOpcode::SubSpImm ||
               opc == MOpcode::AddSpImm || opc == MOpcode::PhiStoreGPR ||
               opc == MOpcode::PhiStoreFPR;
    };

    // Build predecessor map from CFG edges (branch targets + fallthroughs).
    std::unordered_map<std::size_t, std::vector<std::size_t>> preds;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto &instrs = fn.blocks[i].instrs;
        if (instrs.empty())
            continue;

        // Explicit branch targets.
        for (const auto &mi : instrs) {
            std::string target = getBranchTarget(mi);
            if (!target.empty()) {
                auto it = nameToIdx.find(target);
                if (it != nameToIdx.end())
                    preds[it->second].push_back(i);
            }
        }

        // Fallthrough edge: if last instr is not unconditional branch or ret,
        // execution falls through to the next block.
        if (i + 1 < fn.blocks.size()) {
            const auto &last = instrs.back();
            if (last.opc != MOpcode::Br && last.opc != MOpcode::Ret)
                preds[i + 1].push_back(i);
        }
    }

    // Compute natural loop body from a back-edge (latch -> header).
    // Uses the standard reverse-reachability algorithm: start from the latch,
    // walk predecessors until reaching the header to find all blocks on paths
    // from header to latch.
    auto computeLoopBody = [&preds](std::size_t header,
                                    std::size_t latch) -> std::unordered_set<std::size_t> {
        std::unordered_set<std::size_t> body;
        body.insert(header);
        if (body.count(latch))
            return body; // Single-block loop.

        std::vector<std::size_t> worklist;
        worklist.push_back(latch);
        body.insert(latch);

        while (!worklist.empty()) {
            std::size_t b = worklist.back();
            worklist.pop_back();
            auto pit = preds.find(b);
            if (pit == preds.end())
                continue;
            for (std::size_t pred : pit->second) {
                // Only include predecessors at or after the header in layout.
                // This prevents the BFS from crawling backwards past the header
                // (e.g. for BASIC two-header for-loops where for_head_neg's BFS
                // would otherwise traverse through for_head_pos and beyond).
                if (pred >= header && !body.count(pred)) {
                    body.insert(pred);
                    worklist.push_back(pred);
                }
            }
        }
        return body;
    };

    struct LoopInfo {
        std::size_t header;
        std::size_t latch;
        std::unordered_set<std::size_t> body;
    };

    std::vector<LoopInfo> loops;
    std::unordered_set<std::size_t> seenHeaders;

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto &instrs = fn.blocks[i].instrs;
        for (const auto &mi : instrs) {
            std::string target = getBranchTarget(mi);
            if (target.empty())
                continue;

            auto it = nameToIdx.find(target);
            if (it != nameToIdx.end() && it->second < i) {
                if (seenHeaders.insert(it->second).second)
                    loops.push_back({it->second, i, computeLoopBody(it->second, i)});
            }
        }
    }

    if (loops.empty())
        return 0;

    std::unordered_map<uint32_t, int64_t> globallyHoisted;

    std::size_t hoisted = 0;

    for (const auto &loop : loops) {
        if (loop.header == 0)
            continue;

        // Skip "loops" whose header block contains a Ret instruction.
        // A block with Ret is a function exit, not a real loop header.
        // Back-edges to such blocks are exit paths, not iteration edges.
        {
            bool headerHasRet = false;
            if (loop.header < fn.blocks.size()) {
                for (const auto &mi : fn.blocks[loop.header].instrs) {
                    if (mi.opc == MOpcode::Ret) {
                        headerHasRet = true;
                        break;
                    }
                }
            }
            if (headerHasRet)
                continue;
        }

        // Skip "loops" whose header has multiple predecessors from outside the loop.
        // These are typically if/else merge points misidentified as loop headers.
        // A true loop header has exactly one entry edge from outside the loop (the
        // preheader) plus one back-edge from within the loop (the latch).
        {
            auto pit = preds.find(loop.header);
            if (pit != preds.end()) {
                int outsidePreds = 0;
                for (std::size_t p : pit->second) {
                    if (!loop.body.count(p))
                        ++outsidePreds;
                }
                if (outsidePreds > 1)
                    continue; // merge point, not a proper loop header
            }
        }

        const std::size_t preIdx = loop.header - 1;

        bool preInLoop = false;
        for (const auto &other : loops) {
            if (&other == &loop)
                continue;
            if (other.body.count(preIdx)) {
                preInLoop = true;
                break;
            }
        }
        if (preInLoop)
            continue;
        // Also skip if preIdx is inside THIS loop's own body.
        if (loop.body.count(preIdx))
            continue;

        auto &preBlock = fn.blocks[preIdx];
        if (preBlock.instrs.empty())
            continue;

        {
            bool reachesHeader = false;
            const auto &lastInstr = preBlock.instrs.back();
            std::string lastTarget = getBranchTarget(lastInstr);

            if (lastTarget.empty() && lastInstr.opc != MOpcode::Ret) {
                // Block falls through to the next block (the loop header).
                // Ret does NOT fall through — it exits the function.
                reachesHeader = true;
            } else {
                auto tgtIt = nameToIdx.find(lastTarget);
                if (tgtIt != nameToIdx.end() && tgtIt->second == loop.header)
                    reachesHeader = true;
                if (lastInstr.opc == MOpcode::BCond || lastInstr.opc == MOpcode::Cbz ||
                    lastInstr.opc == MOpcode::Cbnz)
                    reachesHeader = true;
            }

            if (!reachesHeader)
                continue;
        }

        struct RegInfo {
            std::size_t movriCount{0};
            std::size_t otherDefCount{0};
            std::size_t useWithoutDefBlocks{0}; // blocks that USE but don't DEFINE
            int64_t immValue{0};
        };

        std::unordered_map<uint32_t, RegInfo> regDefs;

        for (std::size_t bi : loop.body) {
            if (bi >= fn.blocks.size())
                continue;
            const auto &instrs = fn.blocks[bi].instrs;

            // Per-block: track which callee-saved GPRs are defined vs used
            std::unordered_set<uint32_t> definedInBlock;
            std::unordered_set<uint32_t> usedInBlock;
            for (const auto &mi : instrs) {
                if (mi.opc == MOpcode::MovRI && mi.ops.size() >= 2 && isPhysReg(mi.ops[0]) &&
                    mi.ops[0].reg.cls == RegClass::GPR && isCalleeSavedGPR(mi.ops[0].reg.idOrPhys))
                    definedInBlock.insert(mi.ops[0].reg.idOrPhys);
                // Check uses (non-def operands)
                std::size_t startOp = isNonDefOpc(mi.opc) ? 0 : 1;
                for (std::size_t oi = startOp; oi < mi.ops.size(); ++oi) {
                    if (mi.ops[oi].kind == MOperand::Kind::Reg && mi.ops[oi].reg.isPhys &&
                        mi.ops[oi].reg.cls == RegClass::GPR &&
                        isCalleeSavedGPR(mi.ops[oi].reg.idOrPhys))
                        usedInBlock.insert(mi.ops[oi].reg.idOrPhys);
                }
            }
            // Count blocks that USE a register without defining it in the same block
            for (uint32_t r : usedInBlock) {
                if (!definedInBlock.count(r))
                    regDefs[r].useWithoutDefBlocks++;
            }

            for (std::size_t ii = 0; ii < instrs.size(); ++ii) {
                const auto &mi = instrs[ii];
                if (mi.opc == MOpcode::MovRI && mi.ops.size() >= 2 && isPhysReg(mi.ops[0]) &&
                    mi.ops[0].reg.cls == RegClass::GPR && mi.ops[1].kind == MOperand::Kind::Imm) {
                    const uint32_t phys = mi.ops[0].reg.idOrPhys;
                    auto &info = regDefs[phys];
                    if (info.movriCount == 0)
                        info.immValue = mi.ops[1].imm;
                    else if (mi.ops[1].imm != info.immValue)
                        ++info.otherDefCount;
                    ++info.movriCount;
                } else {
                    if (!mi.ops.empty() && isPhysReg(mi.ops[0]) &&
                        mi.ops[0].reg.cls == RegClass::GPR && !isNonDefOpc(mi.opc)) {
                        ++regDefs[mi.ops[0].reg.idOrPhys].otherDefCount;
                    }
                    if (mi.opc == MOpcode::Bl || mi.opc == MOpcode::Blr) {
                        for (uint32_t r = static_cast<uint32_t>(PhysReg::X0);
                             r <= static_cast<uint32_t>(PhysReg::X17);
                             ++r)
                            ++regDefs[r].otherDefCount;
                    }
                }
            }
        }

        auto &preInstrs = preBlock.instrs;
        std::size_t insertIdx = preInstrs.size();
        while (insertIdx > 0) {
            const auto opc = preInstrs[insertIdx - 1].opc;
            if (opc == MOpcode::Br || opc == MOpcode::BCond || opc == MOpcode::Cbz ||
                opc == MOpcode::Cbnz || opc == MOpcode::Ret)
                --insertIdx;
            else
                break;
        }

        for (auto &[phys, info] : regDefs) {
            if (info.movriCount == 0 || info.otherDefCount > 0)
                continue;
            if (!isCalleeSavedGPR(phys))
                continue;
            // If any loop body block uses this register without a local MovRI
            // definition, the hoisted value from the preheader might not reach
            // that block (e.g., mutually exclusive if/else branches where only
            // one side has the MovRI). Refuse to hoist in this case.
            if (info.useWithoutDefBlocks > 0)
                continue;

            auto git = globallyHoisted.find(phys);
            if (git != globallyHoisted.end() && git->second != info.immValue)
                continue;

            bool safeInAllBlocks = true;
            for (std::size_t bi : loop.body) {
                if (bi >= fn.blocks.size())
                    continue;
                const auto &instrs = fn.blocks[bi].instrs;
                for (std::size_t ii = 0; ii < instrs.size(); ++ii) {
                    const auto &mi = instrs[ii];

                    if (mi.opc == MOpcode::MovRI && mi.ops.size() >= 2 && isPhysReg(mi.ops[0]) &&
                        mi.ops[0].reg.cls == RegClass::GPR && mi.ops[0].reg.idOrPhys == phys &&
                        mi.ops[1].kind == MOperand::Kind::Imm && mi.ops[1].imm == info.immValue)
                        break;

                    std::size_t startOp = isNonDefOpc(mi.opc) ? 0 : 1;
                    for (std::size_t oi = startOp; oi < mi.ops.size(); ++oi) {
                        if (mi.ops[oi].kind == MOperand::Kind::Reg && mi.ops[oi].reg.isPhys &&
                            mi.ops[oi].reg.cls == RegClass::GPR &&
                            mi.ops[oi].reg.idOrPhys == phys) {
                            safeInAllBlocks = false;
                            break;
                        }
                    }
                    if (!safeInAllBlocks)
                        break;
                }
                if (!safeInAllBlocks)
                    break;
            }
            if (!safeInAllBlocks)
                continue;

            globallyHoisted[phys] = info.immValue;

            MInstr hoistedMov{
                MOpcode::MovRI,
                {MOperand::regOp(static_cast<PhysReg>(phys)), MOperand::immOp(info.immValue)}};
            preInstrs.insert(preInstrs.begin() + static_cast<std::ptrdiff_t>(insertIdx),
                             hoistedMov);
            ++insertIdx;

            for (std::size_t bi : loop.body) {
                if (bi >= fn.blocks.size())
                    continue;

                // Don't remove MovRI from blocks that have predecessors outside
                // the loop body. Such blocks are reachable from paths where the
                // preheader's hoisted MovRI hasn't executed, so removing the
                // local MovRI would leave the register undefined on those paths.
                {
                    auto pit = preds.find(bi);
                    if (pit != preds.end()) {
                        bool hasOutsidePred = false;
                        for (std::size_t p : pit->second) {
                            if (!loop.body.count(p) && p != preIdx) {
                                hasOutsidePred = true;
                                break;
                            }
                        }
                        if (hasOutsidePred)
                            continue; // preserve MovRI in this block
                    }
                }

                auto &instrs = fn.blocks[bi].instrs;
                auto beforeSize = instrs.size();
                instrs.erase(std::remove_if(instrs.begin(),
                                            instrs.end(),
                                            [phys, &info](const MInstr &mi) {
                                                return mi.opc == MOpcode::MovRI &&
                                                       mi.ops.size() >= 2 && isPhysReg(mi.ops[0]) &&
                                                       mi.ops[0].reg.cls == RegClass::GPR &&
                                                       mi.ops[0].reg.idOrPhys == phys &&
                                                       mi.ops[1].kind == MOperand::Kind::Imm &&
                                                       mi.ops[1].imm == info.immValue;
                                            }),
                             instrs.end());
                (void)beforeSize;
            }

            // Re-validate insertIdx after erase (defensive: if preIdx were
            // somehow in the loop body, the erase could shrink preInstrs).
            if (insertIdx > preInstrs.size())
                insertIdx = preInstrs.size();

            ++hoisted;
        }
    }

    return hoisted;
}

std::size_t eliminateLoopPhiSpills(MFunction &fn) {
    if (fn.blocks.size() < 2)
        return 0;

    // Build block-name -> block-index map.
    std::unordered_map<std::string, std::size_t> nameToIdx;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        nameToIdx[fn.blocks[i].name] = i;

    // Helper: get branch target label from an instruction.
    auto getBranchTarget = [](const MInstr &mi) -> std::string {
        if (mi.opc == MOpcode::Br && !mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Label)
            return mi.ops[0].label;
        if (mi.opc == MOpcode::BCond && mi.ops.size() >= 2 &&
            mi.ops[1].kind == MOperand::Kind::Label)
            return mi.ops[1].label;
        if ((mi.opc == MOpcode::Cbz || mi.opc == MOpcode::Cbnz) && mi.ops.size() >= 2 &&
            mi.ops[1].kind == MOperand::Kind::Label)
            return mi.ops[1].label;
        return {};
    };

    // Helper: check if instruction is a branch or terminator.
    auto isTerminator = [](MOpcode opc) -> bool {
        return opc == MOpcode::Br || opc == MOpcode::BCond || opc == MOpcode::Cbz ||
               opc == MOpcode::Cbnz || opc == MOpcode::Ret;
    };

    // Find back-edges: block i branches to block j where j <= i.
    struct BackEdge {
        std::size_t latchIdx;
        std::size_t headerIdx;
    };

    std::vector<BackEdge> backEdges;

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        for (const auto &mi : fn.blocks[i].instrs) {
            std::string target = getBranchTarget(mi);
            if (target.empty())
                continue;
            auto it = nameToIdx.find(target);
            if (it != nameToIdx.end() && it->second <= i)
                backEdges.push_back({i, it->second});
        }
    }

    if (backEdges.empty())
        return 0;

    std::size_t eliminated = 0;

    // Process each back-edge. We process at most one per pass to avoid
    // invalidating indices after block insertion.
    for (const auto &edge : backEdges) {
        auto &header = fn.blocks[edge.headerIdx];
        auto &latch = fn.blocks[edge.latchIdx];

        // Step 1: Identify phi loads at the start of the header.
        // These are LdrRegFpImm instructions at the very beginning.
        struct PhiLoad {
            std::size_t instrIdx;
            int64_t fpOffset;
            MOperand dstReg; // Physical register loaded into.
        };

        std::vector<PhiLoad> phiLoads;

        for (std::size_t i = 0; i < header.instrs.size(); ++i) {
            const auto &mi = header.instrs[i];
            if (mi.opc != MOpcode::LdrRegFpImm)
                break; // Phi loads must be at the very start.
            if (mi.ops.size() < 2 || !isPhysReg(mi.ops[0]) || mi.ops[1].kind != MOperand::Kind::Imm)
                break;
            phiLoads.push_back({i, mi.ops[1].imm, mi.ops[0]});
        }

        // Require at least 2 consecutive phi loads. Single-variable loops
        // often use register movs for phi transfer (from single-predecessor
        // optimization), making header splitting unsafe.
        if (phiLoads.size() < 2)
            continue;

        // Step 2: Find matching phi stores in the latch block.
        // These are StrRegFpImm instructions that store to the same offsets
        // as the header's phi loads. They may not be strictly at the end
        // (other instructions like cmp can be interspersed).
        struct PhiStore {
            std::size_t instrIdx;
            int64_t fpOffset;
            MOperand srcReg; // Physical register stored from.
        };

        // Collect phi load offsets for matching.
        std::unordered_set<int64_t> phiLoadOffsets;
        for (const auto &pl : phiLoads)
            phiLoadOffsets.insert(pl.fpOffset);

        // Scan the entire latch block for stores to phi slot offsets.
        std::vector<PhiStore> phiStores;
        for (std::size_t i = 0; i < latch.instrs.size(); ++i) {
            const auto &mi = latch.instrs[i];
            if (mi.opc == MOpcode::StrRegFpImm && mi.ops.size() >= 2 && isPhysReg(mi.ops[0]) &&
                mi.ops[1].kind == MOperand::Kind::Imm && phiLoadOffsets.count(mi.ops[1].imm)) {
                phiStores.push_back({i, mi.ops[1].imm, mi.ops[0]});
            }
        }

        if (phiStores.empty())
            continue;

        // Step 3: Match phi loads with phi stores by FP offset.
        struct PhiPair {
            PhiLoad load;
            PhiStore store;
        };

        std::vector<PhiPair> pairs;

        for (const auto &load : phiLoads) {
            for (const auto &store : phiStores) {
                if (load.fpOffset == store.fpOffset) {
                    pairs.push_back({load, store});
                    break;
                }
            }
        }

        if (pairs.empty())
            continue;

        // Safety: require a 1:1 match between phi loads and ALL phi-like stores.
        if (pairs.size() != phiLoads.size())
            continue;

        // Count ALL StrRegFpImm stores in the block (not just those matching
        // phi load offsets). If there are stores to FP offsets that DON'T have
        // a matching phi load, some loop-carried values are transferred via
        // different mechanisms (register movs, etc.) and splitting the header
        // would break those transfers.
        {
            std::unordered_set<int64_t> matchedOffsets;
            for (const auto &p : pairs)
                matchedOffsets.insert(p.load.fpOffset);

            // Collect ALL unique FP store offsets in the latch block.
            std::unordered_set<int64_t> allStoreOffsets;
            for (const auto &mi : latch.instrs) {
                if ((mi.opc == MOpcode::StrRegFpImm || mi.opc == MOpcode::PhiStoreGPR) &&
                    mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Imm) {
                    allStoreOffsets.insert(mi.ops[1].imm);
                }
            }

            // Check that EVERY phi-slot store is matched by a phi load.
            // Offsets that are stored but not loaded indicate loop-carried
            // values transferred via register moves, not stack loads.
            bool hasUnmatchedStores = false;
            for (int64_t off : allStoreOffsets) {
                // Skip stores to offsets that are NOT phi slots (e.g., exit-path values).
                // Only flag stores that correspond to a phi slot used at block entry.
                // We detect phi slots by checking if the offset was stored FROM the entry
                // block as well. But since we can't easily check that, use a simpler
                // heuristic: only flag offsets that are loaded at the header start
                // (within the first few instructions, not just the phi loads).
                // Actually, just check: is this offset loaded ANYWHERE in the header block?
                bool loadedInHeader = false;
                for (const auto &hmi : header.instrs) {
                    if (hmi.opc == MOpcode::LdrRegFpImm && hmi.ops.size() >= 2 &&
                        hmi.ops[1].kind == MOperand::Kind::Imm && hmi.ops[1].imm == off) {
                        loadedInHeader = true;
                        break;
                    }
                }
                if (loadedInHeader && !matchedOffsets.count(off)) {
                    hasUnmatchedStores = true;
                    break;
                }
            }
            if (hasUnmatchedStores)
                continue;
        }

        // Step 4: Verify the phi slot offsets are NOT loaded anywhere else in the
        // function besides the header. If the exit block loads from the same offset,
        // we must keep the store.
        std::unordered_set<int64_t> phiOffsets;
        for (const auto &p : pairs)
            phiOffsets.insert(p.load.fpOffset);

        bool safeToEliminate = true;
        for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
            if (bi == edge.headerIdx)
                continue; // Skip header (its loads are the ones we're eliminating).
            for (const auto &mi : fn.blocks[bi].instrs) {
                if (mi.opc == MOpcode::LdrRegFpImm && mi.ops.size() >= 2 &&
                    mi.ops[1].kind == MOperand::Kind::Imm && phiOffsets.count(mi.ops[1].imm)) {
                    safeToEliminate = false;
                    break;
                }
                // Also check LDP loads.
                if (mi.opc == MOpcode::LdpRegFpImm && mi.ops.size() >= 3 &&
                    mi.ops[2].kind == MOperand::Kind::Imm) {
                    int64_t off1 = mi.ops[2].imm;
                    int64_t off2 = off1 + 8;
                    if (phiOffsets.count(off1) || phiOffsets.count(off2)) {
                        safeToEliminate = false;
                        break;
                    }
                }
            }
            if (!safeToEliminate)
                break;
        }

        if (!safeToEliminate)
            continue;

        // Step 5: Split the header block. Create a new body block that contains
        // everything after the phi loads. The back-edge will target this body block.
        std::string bodyName = header.name + "_body";
        std::string headerName = header.name;

        // Build the body block with instructions after the phi loads.
        MBasicBlock bodyBlock;
        bodyBlock.name = bodyName;
        std::size_t firstNonPhiIdx = phiLoads.back().instrIdx + 1;
        bodyBlock.instrs.assign(header.instrs.begin() + static_cast<std::ptrdiff_t>(firstNonPhiIdx),
                                header.instrs.end());

        // Trim the header to just the phi loads + unconditional branch to body.
        header.instrs.resize(firstNonPhiIdx);
        header.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(bodyName)}});

        // Step 6: The back-edge and phi stores are now in the BODY block
        // (they were moved from the header during the split). Scan the entire
        // body block for stores to phi slot offsets.
        std::vector<PhiStore> bodyPhiStores;
        for (std::size_t i = 0; i < bodyBlock.instrs.size(); ++i) {
            const auto &mi = bodyBlock.instrs[i];
            if (mi.opc == MOpcode::StrRegFpImm && mi.ops.size() >= 2 && isPhysReg(mi.ops[0]) &&
                mi.ops[1].kind == MOperand::Kind::Imm && phiLoadOffsets.count(mi.ops[1].imm)) {
                bodyPhiStores.push_back({i, mi.ops[1].imm, mi.ops[0]});
            }
        }

        // Re-match pairs using body stores.
        std::unordered_set<std::size_t> storeIndicesToRemove;
        std::vector<MInstr> movs;

        for (const auto &load : phiLoads) {
            for (const auto &store : bodyPhiStores) {
                if (load.fpOffset == store.fpOffset) {
                    storeIndicesToRemove.insert(store.instrIdx);
                    if (store.srcReg.reg.idOrPhys != load.dstReg.reg.idOrPhys) {
                        movs.push_back(MInstr{MOpcode::MovRR, {load.dstReg, store.srcReg}});
                    }
                    ++eliminated;
                    break;
                }
            }
        }

        // Remove phi stores from body block.
        {
            std::vector<MInstr> newInstrs;
            newInstrs.reserve(bodyBlock.instrs.size());
            for (std::size_t i = 0; i < bodyBlock.instrs.size(); ++i) {
                if (storeIndicesToRemove.count(i))
                    continue;
                newInstrs.push_back(std::move(bodyBlock.instrs[i]));
            }
            bodyBlock.instrs = std::move(newInstrs);
        }

        // Redirect back-edge branch targets from header to body (self-loop fix).
        for (auto &mi : bodyBlock.instrs) {
            for (auto &op : mi.ops) {
                if (op.kind == MOperand::Kind::Label && op.label == headerName)
                    op.label = bodyName;
            }
        }

        // Insert movs before the last terminator in the body block.
        if (!movs.empty()) {
            std::size_t insertPos = bodyBlock.instrs.size();
            while (insertPos > 0 && isTerminator(bodyBlock.instrs[insertPos - 1].opc))
                --insertPos;
            bodyBlock.instrs.insert(bodyBlock.instrs.begin() +
                                        static_cast<std::ptrdiff_t>(insertPos),
                                    movs.begin(),
                                    movs.end());
        }

        // Step 7: Insert body block immediately after header.
        fn.blocks.insert(fn.blocks.begin() + static_cast<std::ptrdiff_t>(edge.headerIdx) + 1,
                         std::move(bodyBlock));

        // Only process one back-edge per pass (indices are invalidated).
        break;
    }

    return eliminated;
}

} // namespace viper::codegen::aarch64::peephole
