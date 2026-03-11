//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/MemoryOpt.cpp
// Purpose: Memory optimizations for the AArch64 peephole optimizer: LDP/STP
//          merging, store-load forwarding, and MADD fusion.
//
// Key invariants:
//   - LDP/STP merge only pairs adjacent FP-relative accesses with matching
//     offset alignment.
//   - Store-load forwarding only applies within a basic block scope.
//
// Ownership/Lifetime:
//   - Operates on mutable instruction vectors owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "MemoryOpt.hpp"

#include "PeepholeCommon.hpp"

namespace viper::codegen::aarch64::peephole
{

bool tryLdpStpMerge(std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats)
{
    if (idx + 1 >= instrs.size())
        return false;

    auto &first = instrs[idx];
    auto &second = instrs[idx + 1];

    MOpcode pairOpc;
    bool isLoad = false;
    bool isFPR = false;

    if (first.opc == MOpcode::LdrRegFpImm && second.opc == MOpcode::LdrRegFpImm)
    {
        pairOpc = MOpcode::LdpRegFpImm;
        isLoad = true;
    }
    else if (first.opc == MOpcode::StrRegFpImm && second.opc == MOpcode::StrRegFpImm)
    {
        pairOpc = MOpcode::StpRegFpImm;
    }
    else if (first.opc == MOpcode::LdrFprFpImm && second.opc == MOpcode::LdrFprFpImm)
    {
        pairOpc = MOpcode::LdpFprFpImm;
        isLoad = true;
        isFPR = true;
    }
    else if (first.opc == MOpcode::StrFprFpImm && second.opc == MOpcode::StrFprFpImm)
    {
        pairOpc = MOpcode::StpFprFpImm;
        isFPR = true;
    }
    else
    {
        return false;
    }

    if (first.ops.size() != 2 || second.ops.size() != 2)
        return false;
    if (!isPhysReg(first.ops[0]) || !isPhysReg(second.ops[0]))
        return false;
    if (first.ops[1].kind != MOperand::Kind::Imm || second.ops[1].kind != MOperand::Kind::Imm)
        return false;

    long long off1 = first.ops[1].imm;
    long long off2 = second.ops[1].imm;

    if (off2 != off1 + 8)
        return false;

    if (off1 < -512 || off1 > 504)
        return false;

    if (isLoad)
    {
        if (samePhysReg(first.ops[0], second.ops[0]))
            return false;
    }

    (void)isFPR;

    first.opc = pairOpc;
    MOperand reg1 = first.ops[0];
    MOperand reg2 = second.ops[0];
    MOperand offset = first.ops[1];
    first.ops.clear();
    first.ops.push_back(reg1);
    first.ops.push_back(reg2);
    first.ops.push_back(offset);

    instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(idx + 1));
    ++stats.ldpStpMerges;
    return true;
}

std::size_t forwardStoreLoads(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    std::size_t forwarded = 0;
    for (std::size_t i = 0; i < instrs.size(); ++i)
    {
        const bool isGPR = instrs[i].opc == MOpcode::StrRegFpImm;
        const bool isFPR = instrs[i].opc == MOpcode::StrFprFpImm;
        if (!isGPR && !isFPR)
            continue;
        if (instrs[i].ops.size() < 2)
            continue;
        if (!isPhysReg(instrs[i].ops[0]))
            continue;
        if (instrs[i].ops[1].kind != MOperand::Kind::Imm)
            continue;

        const int64_t storeOff = instrs[i].ops[1].imm;
        const MOperand storeReg = instrs[i].ops[0];
        const MOpcode matchLoad = isGPR ? MOpcode::LdrRegFpImm : MOpcode::LdrFprFpImm;
        const MOpcode matchStore = isGPR ? MOpcode::StrRegFpImm : MOpcode::StrFprFpImm;
        const MOpcode movOpc = isGPR ? MOpcode::MovRR : MOpcode::FMovRR;

        for (std::size_t j = i + 1; j < instrs.size(); ++j)
        {
            const auto &next = instrs[j];

            if (next.opc == matchStore && next.ops.size() >= 2 &&
                next.ops[1].kind == MOperand::Kind::Imm && next.ops[1].imm == storeOff)
                break;

            if (next.opc == matchLoad && next.ops.size() >= 2 &&
                next.ops[1].kind == MOperand::Kind::Imm && next.ops[1].imm == storeOff &&
                isPhysReg(next.ops[0]))
            {
                instrs[j] = MInstr{movOpc, {next.ops[0], storeReg}};
                ++forwarded;
                ++stats.deadInstructionsRemoved;
                continue;
            }

            if (definesReg(next, storeReg))
                break;

            if (next.opc == MOpcode::Bl || next.opc == MOpcode::Blr)
                break;

            if (next.opc == MOpcode::Br || next.opc == MOpcode::BCond || next.opc == MOpcode::Ret ||
                next.opc == MOpcode::Cbz || next.opc == MOpcode::Cbnz)
                break;
        }
    }
    return forwarded;
}

bool tryMaddFusion(std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats)
{
    if (idx + 1 >= instrs.size())
        return false;

    auto &mulInstr = instrs[idx];
    auto &addInstr = instrs[idx + 1];

    if (mulInstr.opc != MOpcode::MulRRR || mulInstr.ops.size() != 3)
        return false;
    if (addInstr.opc != MOpcode::AddRRR || addInstr.ops.size() != 3)
        return false;

    if (!isPhysReg(mulInstr.ops[0]))
        return false;

    const MOperand &mulDst = mulInstr.ops[0];
    const MOperand &mulA = mulInstr.ops[1];
    const MOperand &mulB = mulInstr.ops[2];

    MOperand addend;
    bool mulDstInLhs = samePhysReg(addInstr.ops[1], mulDst);
    bool mulDstInRhs = samePhysReg(addInstr.ops[2], mulDst);

    if (mulDstInLhs && !mulDstInRhs)
    {
        addend = addInstr.ops[2];
    }
    else if (mulDstInRhs && !mulDstInLhs)
    {
        addend = addInstr.ops[1];
    }
    else
    {
        return false;
    }

    for (std::size_t i = idx + 2; i < instrs.size(); ++i)
    {
        if (usesReg(instrs[i], mulDst))
            return false;
        if (definesReg(instrs[i], mulDst))
            break;
    }

    const MOperand addDst = addInstr.ops[0];
    mulInstr.opc = MOpcode::MAddRRRR;
    mulInstr.ops.clear();
    mulInstr.ops.push_back(addDst);
    mulInstr.ops.push_back(mulA);
    mulInstr.ops.push_back(mulB);
    mulInstr.ops.push_back(addend);

    instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(idx + 1));
    ++stats.maddFusions;
    return true;
}

} // namespace viper::codegen::aarch64::peephole
