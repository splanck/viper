//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/CopyPropDCE.cpp
// Purpose: Copy propagation, dead code elimination, dead FP store elimination,
//          dead flag-setter removal, and compute-into-target folding for the
//          AArch64 peephole optimizer.
//
// Key invariants:
//   - Copy propagation does not propagate through ABI registers.
//   - DCE conservatively marks callee-saved and ABI registers as live at exit.
//
// Ownership/Lifetime:
//   - Operates on mutable instruction vectors owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "CopyPropDCE.hpp"

#include "PeepholeCommon.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::aarch64::peephole
{

std::size_t propagateCopies(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    std::unordered_map<uint32_t, MOperand> copyOrigin;
    std::size_t propagated = 0;

    auto invalidateDependents = [&copyOrigin](uint32_t originKey)
    {
        std::vector<uint32_t> toErase;
        for (const auto &[key, origin] : copyOrigin)
        {
            if (regKey(origin) == originKey)
                toErase.push_back(key);
        }
        for (uint32_t key : toErase)
            copyOrigin.erase(key);
    };

    for (auto &instr : instrs)
    {
        if (instr.opc == MOpcode::Br || instr.opc == MOpcode::BCond || instr.opc == MOpcode::Ret ||
            instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr)
        {
            if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr)
                copyOrigin.clear();
            continue;
        }

        // Shared copy-tracking for MovRR and FMovRR (same-class).
        auto trackCopy = [&](MInstr &mi) -> bool
        {
            const MOperand &dst = mi.ops[0];
            const MOperand &src = mi.ops[1];
            uint32_t dstKey = regKey(dst);

            invalidateDependents(dstKey);
            copyOrigin.erase(dstKey);

            MOperand origin = src;
            if (!isABIReg(src))
            {
                auto it = copyOrigin.find(regKey(src));
                if (it != copyOrigin.end())
                    origin = it->second;
            }

            if (!samePhysReg(dst, origin))
            {
                copyOrigin[dstKey] = origin;
                if (!samePhysReg(src, origin))
                {
                    mi.ops[1] = origin;
                    ++propagated;
                }
            }
            return true;
        };

        // MovRR copy tracking
        if (instr.opc == MOpcode::MovRR && instr.ops.size() == 2 && isPhysReg(instr.ops[0]) &&
            isPhysReg(instr.ops[1]))
        {
            trackCopy(instr);
            continue;
        }

        // FMovRR copy tracking (same-class only)
        if (instr.opc == MOpcode::FMovRR && instr.ops.size() == 2 && isPhysReg(instr.ops[0]) &&
            isPhysReg(instr.ops[1]) && instr.ops[0].reg.cls == instr.ops[1].reg.cls)
        {
            trackCopy(instr);
            continue;
        }

        // Propagate uses BEFORE invalidating defs
        for (std::size_t i = 0; i < instr.ops.size(); ++i)
        {
            auto &op = instr.ops[i];
            if (!isPhysReg(op))
                continue;

            auto [isUse, isDef] = classifyOperand(instr, i);
            if (isUse && !isDef && !isABIReg(op))
            {
                uint32_t key = regKey(op);
                auto it = copyOrigin.find(key);
                if (it != copyOrigin.end() && !samePhysReg(op, it->second))
                {
                    op = it->second;
                    ++propagated;
                }
            }
        }

        // Invalidate definitions
        for (std::size_t i = 0; i < instr.ops.size(); ++i)
        {
            const auto &op = instr.ops[i];
            if (!isPhysReg(op))
                continue;

            auto [isUse, isDef] = classifyOperand(instr, i);
            if (isDef)
            {
                uint32_t key = regKey(op);
                invalidateDependents(key);
                copyOrigin.erase(key);
            }
        }
    }

    stats.copiesPropagated += static_cast<int>(propagated);
    return propagated;
}

std::size_t removeDeadInstructions(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    if (instrs.empty())
        return 0;

    std::unordered_set<uint32_t> liveRegs;

    // Mark argument registers as live at block exit
    for (int i = 0; i <= 7; ++i)
    {
        liveRegs.insert((static_cast<uint32_t>(RegClass::GPR) << 16) |
                        static_cast<uint32_t>(PhysReg::X0) + i);
    }
    for (int i = 0; i <= 7; ++i)
    {
        liveRegs.insert((static_cast<uint32_t>(RegClass::FPR) << 16) |
                        static_cast<uint32_t>(PhysReg::V0) + i);
    }

    // Mark callee-saved GPRs (x19-x28) as live at block exit
    for (uint32_t r = static_cast<uint32_t>(PhysReg::X19); r <= static_cast<uint32_t>(PhysReg::X28);
         ++r)
    {
        liveRegs.insert((static_cast<uint32_t>(RegClass::GPR) << 16) | r);
    }

    std::vector<bool> toRemove(instrs.size(), false);
    std::size_t removed = 0;

    for (std::size_t i = instrs.size(); i > 0; --i)
    {
        const std::size_t idx = i - 1;
        const auto &instr = instrs[idx];

        if (hasSideEffects(instr))
        {
            for (std::size_t j = 0; j < instr.ops.size(); ++j)
            {
                const auto &op = instr.ops[j];
                if (isPhysReg(op))
                {
                    auto [isUse, isDef] = classifyOperand(instr, j);
                    if (isUse)
                        liveRegs.insert(regKey(op));
                }
            }
            continue;
        }

        auto defReg = getDefinedReg(instr);
        if (defReg)
        {
            uint32_t key = regKey(*defReg);
            if (liveRegs.find(key) == liveRegs.end())
            {
                toRemove[idx] = true;
                ++removed;
                continue;
            }

            liveRegs.erase(key);
        }

        for (std::size_t j = 0; j < instr.ops.size(); ++j)
        {
            const auto &op = instr.ops[j];
            if (isPhysReg(op))
            {
                auto [isUse, isDef] = classifyOperand(instr, j);
                if (isUse)
                    liveRegs.insert(regKey(op));
            }
        }
    }

    if (removed > 0)
    {
        removeMarkedInstructions(instrs, toRemove);
        stats.deadInstructionsRemoved += static_cast<int>(removed);
    }

    return removed;
}

std::size_t eliminateDeadFpStores(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    std::unordered_map<int64_t, std::size_t> lastStore;
    std::vector<bool> toRemove(instrs.size(), false);
    std::size_t removed = 0;

    for (std::size_t i = 0; i < instrs.size(); ++i)
    {
        const auto &instr = instrs[i];

        if ((instr.opc == MOpcode::StrRegFpImm || instr.opc == MOpcode::StrFprFpImm) &&
            instr.ops.size() >= 2 && instr.ops[1].kind == MOperand::Kind::Imm)
        {
            const int64_t offset = instr.ops[1].imm;
            auto it = lastStore.find(offset);
            if (it != lastStore.end())
            {
                toRemove[it->second] = true;
                ++removed;
            }
            lastStore[offset] = i;
            continue;
        }

        if ((instr.opc == MOpcode::LdrRegFpImm || instr.opc == MOpcode::LdrFprFpImm) &&
            instr.ops.size() >= 2 && instr.ops[1].kind == MOperand::Kind::Imm)
        {
            lastStore.erase(instr.ops[1].imm);
            continue;
        }

        if ((instr.opc == MOpcode::StpRegFpImm || instr.opc == MOpcode::StpFprFpImm) &&
            instr.ops.size() >= 3 && instr.ops[2].kind == MOperand::Kind::Imm)
        {
            const int64_t off = instr.ops[2].imm;
            lastStore.erase(off);
            lastStore.erase(off + 8);
            continue;
        }
        if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
            instr.ops.size() >= 3 && instr.ops[2].kind == MOperand::Kind::Imm)
        {
            lastStore.erase(instr.ops[2].imm);
            lastStore.erase(instr.ops[2].imm + 8);
            continue;
        }

        if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr)
        {
            lastStore.clear();
            continue;
        }

        if (instr.opc == MOpcode::Br || instr.opc == MOpcode::BCond || instr.opc == MOpcode::Ret ||
            instr.opc == MOpcode::Cbz || instr.opc == MOpcode::Cbnz)
        {
            lastStore.clear();
            continue;
        }
    }

    if (removed > 0)
    {
        removeMarkedInstructions(instrs, toRemove);
        stats.deadInstructionsRemoved += static_cast<int>(removed);
    }
    return removed;
}

std::size_t removeDeadFlagSetters(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    auto setsFlags = [](MOpcode opc) -> bool
    {
        switch (opc)
        {
            case MOpcode::CmpRR:
            case MOpcode::CmpRI:
            case MOpcode::TstRR:
            case MOpcode::FCmpRR:
            case MOpcode::AddsRRR:
            case MOpcode::SubsRRR:
            case MOpcode::AddsRI:
            case MOpcode::SubsRI:
                return true;
            default:
                return false;
        }
    };

    auto readsFlags = [](MOpcode opc) -> bool
    {
        switch (opc)
        {
            case MOpcode::BCond:
            case MOpcode::Cset:
            case MOpcode::Csel:
                return true;
            default:
                return false;
        }
    };

    std::vector<bool> toRemove(instrs.size(), false);
    std::size_t removed = 0;

    for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
    {
        if (!setsFlags(instrs[i].opc))
            continue;

        bool isDead = false;
        for (std::size_t j = i + 1; j < instrs.size(); ++j)
        {
            if (readsFlags(instrs[j].opc))
                break;
            if (setsFlags(instrs[j].opc))
            {
                if (instrs[i].opc == MOpcode::CmpRI || instrs[i].opc == MOpcode::CmpRR ||
                    instrs[i].opc == MOpcode::TstRR || instrs[i].opc == MOpcode::FCmpRR)
                {
                    isDead = true;
                }
                break;
            }
        }
        if (isDead)
        {
            toRemove[i] = true;
            ++removed;
        }
    }

    if (removed > 0)
    {
        removeMarkedInstructions(instrs, toRemove);
        stats.deadInstructionsRemoved += static_cast<int>(removed);
    }
    return removed;
}

std::size_t foldComputeIntoTarget(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    auto isSimpleALU = [](MOpcode opc) -> bool
    {
        switch (opc)
        {
            case MOpcode::AddRRR:
            case MOpcode::SubRRR:
            case MOpcode::AddRI:
            case MOpcode::SubRI:
            case MOpcode::AddsRRR:
            case MOpcode::SubsRRR:
            case MOpcode::AddsRI:
            case MOpcode::SubsRI:
            case MOpcode::MulRRR:
            case MOpcode::SmulhRRR:
            case MOpcode::AndRRR:
            case MOpcode::OrrRRR:
            case MOpcode::EorRRR:
            case MOpcode::LslRI:
            case MOpcode::LsrRI:
            case MOpcode::AsrRI:
                return true;
            default:
                return false;
        }
    };

    std::size_t folded = 0;
    for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
    {
        if (!isSimpleALU(instrs[i].opc))
            continue;
        if (instrs[i].ops.empty() || !isPhysReg(instrs[i].ops[0]))
            continue;

        const MOperand aluDst = instrs[i].ops[0];

        std::size_t movIdx = i + 1;
        while (movIdx < instrs.size() && instrs[movIdx].opc == MOpcode::BCond)
            ++movIdx;

        if (movIdx >= instrs.size())
            continue;
        if (instrs[movIdx].opc != MOpcode::MovRR)
            continue;
        if (instrs[movIdx].ops.size() != 2)
            continue;
        if (!samePhysReg(instrs[movIdx].ops[1], aluDst))
            continue;
        if (!isPhysReg(instrs[movIdx].ops[0]))
            continue;

        const MOperand movDst = instrs[movIdx].ops[0];

        bool interveningUse = false;
        for (std::size_t k = i + 1; k < movIdx; ++k)
        {
            if (usesReg(instrs[k], movDst))
            {
                interveningUse = true;
                break;
            }
        }
        if (interveningUse)
            continue;

        bool aluDstDead = true;
        for (std::size_t j = movIdx + 1; j < instrs.size(); ++j)
        {
            if (usesReg(instrs[j], aluDst))
            {
                aluDstDead = false;
                break;
            }
            if (definesReg(instrs[j], aluDst))
                break;
        }
        if (!aluDstDead)
            continue;

        instrs[i].ops[0] = movDst;
        instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(movIdx));
        ++folded;
        ++stats.deadInstructionsRemoved;

        if (i > 0)
            --i;
    }
    return folded;
}

std::size_t eliminateDeadFpStoresCrossBlock(MFunction &fn, PeepholeStats &stats)
{
    // Step 1: Collect all FP offsets that are LOADED anywhere in the function.
    std::unordered_set<int64_t> loadedOffsets;
    for (const auto &bb : fn.blocks)
    {
        for (const auto &mi : bb.instrs)
        {
            if (mi.opc == MOpcode::LdrRegFpImm && mi.ops.size() >= 2 &&
                mi.ops[1].kind == MOperand::Kind::Imm)
            {
                loadedOffsets.insert(mi.ops[1].imm);
            }
            if (mi.opc == MOpcode::LdpRegFpImm && mi.ops.size() >= 3 &&
                mi.ops[2].kind == MOperand::Kind::Imm)
            {
                loadedOffsets.insert(mi.ops[2].imm);
                loadedOffsets.insert(mi.ops[2].imm + 8);
            }
        }
    }

    // Step 2: Remove stores to offsets that are never loaded.
    std::size_t removed = 0;
    for (auto &bb : fn.blocks)
    {
        bb.instrs.erase(std::remove_if(bb.instrs.begin(),
                                       bb.instrs.end(),
                                       [&](const MInstr &mi)
                                       {
                                           if (mi.opc == MOpcode::StrRegFpImm &&
                                               mi.ops.size() >= 2 &&
                                               mi.ops[1].kind == MOperand::Kind::Imm &&
                                               !loadedOffsets.count(mi.ops[1].imm))
                                           {
                                               ++removed;
                                               return true;
                                           }
                                           return false;
                                       }),
                        bb.instrs.end());
    }
    stats.deadInstructionsRemoved += static_cast<int>(removed);
    return removed;
}

} // namespace viper::codegen::aarch64::peephole
