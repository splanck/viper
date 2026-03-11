//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/StrengthReduce.cpp
// Purpose: Arithmetic identity elimination, strength reduction (mul/udiv to
//          shifts), cmp-zero-to-tst, and immediate folding for the AArch64
//          peephole optimizer.
//
// Key invariants:
//   - Rewrites preserve semantic equivalence under the AArch64 ISA.
//   - Strength reduction only applies to provably equivalent transforms.
//
// Ownership/Lifetime:
//   - Operates on mutable instructions owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "StrengthReduce.hpp"

#include "PeepholeCommon.hpp"

namespace viper::codegen::aarch64::peephole
{
namespace
{

/// @brief Check if a power of 2 and return the log2, or -1 if not.
[[nodiscard]] int log2IfPowerOf2(long long value) noexcept
{
    if (value <= 0)
        return -1;
    if ((value & (value - 1)) != 0)
        return -1;
    int log = 0;
    while ((1LL << log) < value)
        ++log;
    return log;
}

} // namespace

bool tryCmpZeroToTst(MInstr &instr, PeepholeStats &stats)
{
    if (instr.opc != MOpcode::CmpRI)
        return false;
    if (instr.ops.size() != 2)
        return false;
    if (!isPhysReg(instr.ops[0]) || !isImmValue(instr.ops[1], 0))
        return false;

    instr.opc = MOpcode::TstRR;
    instr.ops[1] = instr.ops[0];
    ++stats.cmpZeroToTst;
    return true;
}

bool tryArithmeticIdentity(MInstr &instr, PeepholeStats &stats)
{
    switch (instr.opc)
    {
        case MOpcode::AddRI:
        case MOpcode::SubRI:
            if (instr.ops.size() == 3 && isImmValue(instr.ops[2], 0))
            {
                instr.opc = MOpcode::MovRR;
                instr.ops.pop_back();
                ++stats.arithmeticIdentities;
                return true;
            }
            break;

        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
            if (instr.ops.size() == 3 && isImmValue(instr.ops[2], 0))
            {
                instr.opc = MOpcode::MovRR;
                instr.ops.pop_back();
                ++stats.arithmeticIdentities;
                return true;
            }
            break;

        default:
            break;
    }
    return false;
}

bool tryStrengthReduction(MInstr &instr, const RegConstMap &knownConsts, PeepholeStats &stats)
{
    if (instr.opc != MOpcode::MulRRR)
        return false;
    if (instr.ops.size() != 3)
        return false;

    auto lhsConst = getConstValue(instr.ops[1], knownConsts);
    auto rhsConst = getConstValue(instr.ops[2], knownConsts);

    int shiftAmount = -1;
    MOperand otherOperand;

    if (lhsConst)
    {
        int log = log2IfPowerOf2(*lhsConst);
        if (log >= 0 && log <= 63)
        {
            shiftAmount = log;
            otherOperand = instr.ops[2];
        }
    }
    if (shiftAmount < 0 && rhsConst)
    {
        int log = log2IfPowerOf2(*rhsConst);
        if (log >= 0 && log <= 63)
        {
            shiftAmount = log;
            otherOperand = instr.ops[1];
        }
    }

    if (shiftAmount < 0)
        return false;

    instr.opc = MOpcode::LslRI;
    instr.ops[1] = otherOperand;
    instr.ops[2] = MOperand::immOp(shiftAmount);
    ++stats.strengthReductions;
    return true;
}

bool tryDivStrengthReduction(MInstr &instr, const RegConstMap &knownConsts, PeepholeStats &stats)
{
    if (instr.opc != MOpcode::UDivRRR)
        return false;
    if (instr.ops.size() != 3)
        return false;

    auto rhsConst = getConstValue(instr.ops[2], knownConsts);
    if (!rhsConst || *rhsConst <= 0)
        return false;

    int log = log2IfPowerOf2(*rhsConst);
    if (log < 0 || log > 63)
        return false;

    instr.opc = MOpcode::LsrRI;
    instr.ops[2] = MOperand::immOp(log);
    ++stats.strengthReductions;
    return true;
}

bool tryImmediateFolding(MInstr &instr, const RegConstMap &knownConsts, PeepholeStats &stats)
{
    if (instr.ops.size() != 3)
        return false;

    MOpcode riOpc;
    switch (instr.opc)
    {
        case MOpcode::AddRRR:
            riOpc = MOpcode::AddRI;
            break;
        case MOpcode::SubRRR:
            riOpc = MOpcode::SubRI;
            break;
        default:
            return false;
    }

    auto rhsConst = getConstValue(instr.ops[2], knownConsts);
    if (!rhsConst)
        return false;

    long long val = *rhsConst;
    if (val < 0 || val > 4095)
        return false;

    instr.opc = riOpc;
    instr.ops[2] = MOperand::immOp(val);
    ++stats.immFoldings;
    return true;
}

bool tryFPArithmeticIdentity([[maybe_unused]] MInstr &instr, [[maybe_unused]] PeepholeStats &stats)
{
    return false;
}

} // namespace viper::codegen::aarch64::peephole
