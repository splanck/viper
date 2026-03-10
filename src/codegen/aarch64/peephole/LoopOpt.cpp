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

namespace viper::codegen::aarch64::peephole
{
namespace
{

/// @brief Check whether a physical register is callee-saved (x19-x28).
[[nodiscard]] bool isCalleeSavedGPR(uint32_t phys) noexcept
{
    return phys >= static_cast<uint32_t>(PhysReg::X19) && phys <= static_cast<uint32_t>(PhysReg::X28);
}

} // namespace

std::size_t hoistLoopConstants(MFunction &fn)
{
    if (fn.blocks.size() < 3)
        return 0;

    // Build block-name -> block-index map.
    std::unordered_map<std::string, std::size_t> nameToIdx;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        nameToIdx[fn.blocks[i].name] = i;

    auto getBranchTarget = [](const MInstr &mi) -> std::string
    {
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

    auto isNonDefOpc = [](MOpcode opc) -> bool
    {
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
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
    {
        const auto &instrs = fn.blocks[i].instrs;
        if (instrs.empty())
            continue;

        // Explicit branch targets.
        for (const auto &mi : instrs)
        {
            std::string target = getBranchTarget(mi);
            if (!target.empty())
            {
                auto it = nameToIdx.find(target);
                if (it != nameToIdx.end())
                    preds[it->second].push_back(i);
            }
        }

        // Fallthrough edge: if last instr is not unconditional branch or ret,
        // execution falls through to the next block.
        if (i + 1 < fn.blocks.size())
        {
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
                                    std::size_t latch) -> std::unordered_set<std::size_t>
    {
        std::unordered_set<std::size_t> body;
        body.insert(header);
        if (body.count(latch))
            return body; // Single-block loop.

        std::vector<std::size_t> worklist;
        worklist.push_back(latch);
        body.insert(latch);

        while (!worklist.empty())
        {
            std::size_t b = worklist.back();
            worklist.pop_back();
            auto pit = preds.find(b);
            if (pit == preds.end())
                continue;
            for (std::size_t pred : pit->second)
            {
                // Only include predecessors at or after the header in layout.
                // This prevents the BFS from crawling backwards past the header
                // (e.g. for BASIC two-header for-loops where for_head_neg's BFS
                // would otherwise traverse through for_head_pos and beyond).
                if (pred >= header && !body.count(pred))
                {
                    body.insert(pred);
                    worklist.push_back(pred);
                }
            }
        }
        return body;
    };

    struct LoopInfo
    {
        std::size_t header;
        std::size_t latch;
        std::unordered_set<std::size_t> body;
    };
    std::vector<LoopInfo> loops;
    std::unordered_set<std::size_t> seenHeaders;

    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
    {
        const auto &instrs = fn.blocks[i].instrs;
        for (const auto &mi : instrs)
        {
            std::string target = getBranchTarget(mi);
            if (target.empty())
                continue;

            auto it = nameToIdx.find(target);
            if (it != nameToIdx.end() && it->second < i)
            {
                if (seenHeaders.insert(it->second).second)
                    loops.push_back({it->second, i, computeLoopBody(it->second, i)});
            }
        }
    }

    if (loops.empty())
        return 0;

    std::unordered_map<uint32_t, int64_t> globallyHoisted;

    std::size_t hoisted = 0;

    for (const auto &loop : loops)
    {
        if (loop.header == 0)
            continue;
        const std::size_t preIdx = loop.header - 1;

        bool preInLoop = false;
        for (const auto &other : loops)
        {
            if (&other == &loop)
                continue;
            if (other.body.count(preIdx))
            {
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

            if (lastTarget.empty())
            {
                reachesHeader = true;
            }
            else
            {
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

        struct RegInfo
        {
            std::size_t movriCount{0};
            std::size_t otherDefCount{0};
            int64_t immValue{0};
        };
        std::unordered_map<uint32_t, RegInfo> regDefs;

        for (std::size_t bi : loop.body)
        {
            if (bi >= fn.blocks.size())
                continue;
            const auto &instrs = fn.blocks[bi].instrs;
            for (std::size_t ii = 0; ii < instrs.size(); ++ii)
            {
                const auto &mi = instrs[ii];
                if (mi.opc == MOpcode::MovRI && mi.ops.size() >= 2 && isPhysReg(mi.ops[0]) &&
                    mi.ops[0].reg.cls == RegClass::GPR && mi.ops[1].kind == MOperand::Kind::Imm)
                {
                    const uint32_t phys = mi.ops[0].reg.idOrPhys;
                    auto &info = regDefs[phys];
                    if (info.movriCount == 0)
                        info.immValue = mi.ops[1].imm;
                    else if (mi.ops[1].imm != info.immValue)
                        ++info.otherDefCount;
                    ++info.movriCount;
                }
                else
                {
                    if (!mi.ops.empty() && isPhysReg(mi.ops[0]) &&
                        mi.ops[0].reg.cls == RegClass::GPR && !isNonDefOpc(mi.opc))
                    {
                        ++regDefs[mi.ops[0].reg.idOrPhys].otherDefCount;
                    }
                    if (mi.opc == MOpcode::Bl || mi.opc == MOpcode::Blr)
                    {
                        for (uint32_t r = static_cast<uint32_t>(PhysReg::X0);
                             r <= static_cast<uint32_t>(PhysReg::X17); ++r)
                            ++regDefs[r].otherDefCount;
                    }
                }
            }
        }

        auto &preInstrs = preBlock.instrs;
        std::size_t insertIdx = preInstrs.size();
        while (insertIdx > 0)
        {
            const auto opc = preInstrs[insertIdx - 1].opc;
            if (opc == MOpcode::Br || opc == MOpcode::BCond || opc == MOpcode::Cbz ||
                opc == MOpcode::Cbnz || opc == MOpcode::Ret)
                --insertIdx;
            else
                break;
        }

        for (auto &[phys, info] : regDefs)
        {
            if (info.movriCount == 0 || info.otherDefCount > 0)
                continue;
            if (!isCalleeSavedGPR(phys))
                continue;

            auto git = globallyHoisted.find(phys);
            if (git != globallyHoisted.end() && git->second != info.immValue)
                continue;

            bool safeInAllBlocks = true;
            for (std::size_t bi : loop.body)
            {
                if (bi >= fn.blocks.size())
                    continue;
                const auto &instrs = fn.blocks[bi].instrs;
                for (std::size_t ii = 0; ii < instrs.size(); ++ii)
                {
                    const auto &mi = instrs[ii];

                    if (mi.opc == MOpcode::MovRI && mi.ops.size() >= 2 && isPhysReg(mi.ops[0]) &&
                        mi.ops[0].reg.cls == RegClass::GPR && mi.ops[0].reg.idOrPhys == phys &&
                        mi.ops[1].kind == MOperand::Kind::Imm && mi.ops[1].imm == info.immValue)
                        break;

                    std::size_t startOp = isNonDefOpc(mi.opc) ? 0 : 1;
                    for (std::size_t oi = startOp; oi < mi.ops.size(); ++oi)
                    {
                        if (mi.ops[oi].kind == MOperand::Kind::Reg && mi.ops[oi].reg.isPhys &&
                            mi.ops[oi].reg.cls == RegClass::GPR &&
                            mi.ops[oi].reg.idOrPhys == phys)
                        {
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

            MInstr hoistedMov{MOpcode::MovRI,
                              {MOperand::regOp(static_cast<PhysReg>(phys)),
                               MOperand::immOp(info.immValue)}};
            preInstrs.insert(preInstrs.begin() + static_cast<std::ptrdiff_t>(insertIdx), hoistedMov);
            ++insertIdx;

            for (std::size_t bi : loop.body)
            {
                if (bi >= fn.blocks.size())
                    continue;
                auto &instrs = fn.blocks[bi].instrs;
                instrs.erase(
                    std::remove_if(instrs.begin(), instrs.end(),
                                   [phys, &info](const MInstr &mi)
                                   {
                                       return mi.opc == MOpcode::MovRI && mi.ops.size() >= 2 &&
                                              isPhysReg(mi.ops[0]) &&
                                              mi.ops[0].reg.cls == RegClass::GPR &&
                                              mi.ops[0].reg.idOrPhys == phys &&
                                              mi.ops[1].kind == MOperand::Kind::Imm &&
                                              mi.ops[1].imm == info.immValue;
                                   }),
                    instrs.end());
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

} // namespace viper::codegen::aarch64::peephole
