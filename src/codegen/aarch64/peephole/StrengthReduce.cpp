//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/StrengthReduce.cpp
// Purpose: Arithmetic identity elimination, strength reduction (mul/udiv/sdiv
//          to shifts, div-by-constant to multiply-by-magic), cmp-zero-to-tst,
//          and immediate folding for the AArch64 peephole optimizer.
//
// Key invariants:
//   - Rewrites preserve semantic equivalence under the AArch64 ISA.
//   - Strength reduction only applies to provably equivalent transforms.
//   - Division strength reduction covers:
//     * UDIV by power-of-2 -> LSR (logical shift right)
//     * SDIV by power-of-2 -> ASR with sign correction
//     * SDIV by arbitrary constant -> SMULH + shifts (magic number multiply)
//   - Remainder fusion covers:
//     * UDIV+MSUB (UREM) by power-of-2 -> AND mask
//
// Ownership/Lifetime:
//   - Operates on mutable instructions owned by the caller.
//
// Links: src/codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "StrengthReduce.hpp"

#include "../TargetAArch64.hpp"
#include "PeepholeCommon.hpp"

#include <cstdint>

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

/// @brief Magic number parameters for signed division by constant.
///
/// For a positive divisor d, we compute multiplier M and post-shift S such that:
///   floor(x / d) = floor((x * M) >> (64 + S)) + sign_correction
///
/// The SMULH instruction computes the upper 64 bits of x * M, giving us
/// floor(x * M / 2^64). Combined with a post-shift of S, this yields the
/// quotient.
struct MagicNumber
{
    long long multiplier{0}; ///< Magic multiplier M (signed 64-bit).
    int shift{0};            ///< Post-shift amount S (arithmetic shift right).
    bool needsAdd{false};    ///< True if we need to add the dividend after SMULH
                             ///< (when the multiplier overflows signed 64-bit).
};

/// @brief Compute the magic number for signed division by a constant.
///
/// Algorithm based on Warren's "Hacker's Delight" (2nd edition, §10-4).
/// Given a positive divisor d, find M and S such that for any signed 64-bit x:
///   floor(x / d) = (smulh(x, M) [+ x if needsAdd] >> S) + (x < 0 ? 1 : 0)
///
/// @param d The divisor (must be >= 2).
/// @return Magic number parameters, or empty if d is not suitable.
[[nodiscard]] MagicNumber computeSignedMagic(long long d) noexcept
{
    MagicNumber result{};

    if (d < 2)
        return result;

    // Use unsigned arithmetic for the computation.
    const auto ud = static_cast<uint64_t>(d);

    // Two's complement range for 64-bit: 2^63
    constexpr uint64_t twoP63 = static_cast<uint64_t>(1) << 63;

    // nc = floor((2^63 - 1 - ((2^63) % d)) -- the "magic" threshold
    const uint64_t rem = twoP63 % ud;
    const uint64_t nc = twoP63 - 1 - rem;

    int p = 63;                     // p starts at 63 (we'll increment)
    uint64_t q1 = twoP63 / nc;      // quotient of 2^p / nc
    uint64_t r1 = twoP63 - q1 * nc; // remainder of 2^p / nc
    uint64_t q2 = twoP63 / ud;      // quotient of 2^p / d  (adjusted for sign)
    uint64_t r2 = twoP63 - q2 * ud; // remainder of 2^p / d

    // Iterate to find the right p
    for (;;)
    {
        ++p;
        // Update q1, r1 for 2^p / nc
        q1 = 2 * q1;
        r1 = 2 * r1;
        if (r1 >= nc)
        {
            q1 += 1;
            r1 -= nc;
        }
        // Update q2, r2 for 2^p / d
        q2 = 2 * q2;
        r2 = 2 * r2;
        if (r2 >= ud)
        {
            q2 += 1;
            r2 -= ud;
        }

        const uint64_t delta = ud - r2;
        if (q1 < delta || (q1 == delta && r1 == 0))
            continue;

        break;
    }

    result.multiplier = static_cast<long long>(q2 + 1);
    result.shift = p - 64;

    // If the multiplier would overflow signed 64-bit (i.e., q2+1 >= 2^63),
    // we need to subtract 2^64 from the multiplier and add the dividend
    // after SMULH to compensate.
    if (static_cast<uint64_t>(result.multiplier) >= twoP63)
    {
        // Encode as a negative multiplier and set needsAdd flag.
        // smulh(x, M-2^64) + x == smulh(x, M) since smulh adds the "lost" 2^64*x.
        result.needsAdd = true;
    }

    return result;
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

bool trySDivStrengthReduction(std::vector<MInstr> &instrs,
                              std::size_t idx,
                              const RegConstMap &knownConsts,
                              PeepholeStats &stats)
{
    if (idx >= instrs.size())
        return false;

    auto &divInstr = instrs[idx];
    if (divInstr.opc != MOpcode::SDivRRR || divInstr.ops.size() != 3)
        return false;

    auto rhsConst = getConstValue(divInstr.ops[2], knownConsts);
    if (!rhsConst || *rhsConst <= 1)
        return false;

    const long long divisor = *rhsConst;
    const MOperand dst = divInstr.ops[0];
    const MOperand lhs = divInstr.ops[1];
    const MOperand rhsReg = divInstr.ops[2]; // register holding the constant divisor

    // Verify the divisor register is not live after this instruction.
    // We will reuse it as a temporary register for the expansion.
    if (!isPhysReg(rhsReg))
        return false;

    bool rhsLiveAfter = false;
    for (std::size_t i = idx + 1; i < instrs.size(); ++i)
    {
        if (usesReg(instrs[i], rhsReg))
        {
            rhsLiveAfter = true;
            break;
        }
        if (definesReg(instrs[i], rhsReg))
            break;
    }

    // If divisor reg is used later, check if it's only used by an immediately
    // following MSUB that is part of the same remainder pattern. In that case,
    // we handle it in tryRemainderFusion instead.
    if (rhsLiveAfter)
        return false;

    const int log = log2IfPowerOf2(divisor);

    if (log >= 1 && log <= 63)
    {
        // SDIV by power-of-2: Replace with sign-corrected arithmetic shift.
        //
        // For x / 2^k (signed), the standard sequence is:
        //   asr  tmp, x, #63       ; sign extension: -1 if negative, 0 if positive
        //   lsr  tmp, tmp, #(64-k) ; extract (2^k - 1) if negative, 0 if positive
        //   add  tmp, x, tmp       ; bias: add (2^k-1) to round toward zero
        //   asr  dst, tmp, #k      ; arithmetic shift to divide
        //
        // We use rhsReg as tmp since the divisor constant is no longer needed.

        std::vector<MInstr> expansion;

        // asr tmp, lhs, #63
        expansion.push_back(MInstr{MOpcode::AsrRI, {rhsReg, lhs, MOperand::immOp(63)}});

        // lsr tmp, tmp, #(64-k)
        expansion.push_back(MInstr{MOpcode::LsrRI, {rhsReg, rhsReg, MOperand::immOp(64 - log)}});

        // add tmp, lhs, tmp
        expansion.push_back(MInstr{MOpcode::AddRRR, {rhsReg, lhs, rhsReg}});

        // asr dst, tmp, #k
        expansion.push_back(MInstr{MOpcode::AsrRI, {dst, rhsReg, MOperand::immOp(log)}});

        // Replace the SDivRRR with the expansion
        instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(idx));
        instrs.insert(
            instrs.begin() + static_cast<std::ptrdiff_t>(idx), expansion.begin(), expansion.end());

        ++stats.strengthReductions;
        return true;
    }

    // SDIV by arbitrary positive constant: Replace with magic number multiply.
    //
    // Algorithm: compute magic multiplier M and shift S, then:
    //   mov  tmp, #M              ; load magic multiplier
    //   smulh tmp, lhs, tmp       ; high 64 bits of lhs * M
    //   [add tmp, tmp, lhs]       ; correction if M overflowed (needsAdd)
    //   asr  tmp, tmp, #S         ; post-shift
    //   lsr  sign, tmp, #63       ; extract sign bit (0 or 1)
    //   add  dst, tmp, sign       ; round toward zero for negative values
    //
    // We reuse rhsReg as tmp and dst's register for sign when possible.

    MagicNumber magic = computeSignedMagic(divisor);
    if (magic.multiplier == 0)
        return false;

    // We need a second temporary for the sign correction. We can use dst if
    // dst != lhs (common case). If dst == lhs, we cannot do the transform
    // because we'd clobber lhs before using it.
    if (samePhysReg(dst, lhs))
        return false;

    std::vector<MInstr> expansion;

    // mov tmp, #M (magic multiplier)
    expansion.push_back(MInstr{MOpcode::MovRI, {rhsReg, MOperand::immOp(magic.multiplier)}});

    // smulh tmp, lhs, tmp
    expansion.push_back(MInstr{MOpcode::SmulhRRR, {rhsReg, lhs, rhsReg}});

    // If needsAdd: add tmp, tmp, lhs
    if (magic.needsAdd)
    {
        expansion.push_back(MInstr{MOpcode::AddRRR, {rhsReg, rhsReg, lhs}});
    }

    // asr tmp, tmp, #S (if S > 0)
    if (magic.shift > 0)
    {
        expansion.push_back(MInstr{MOpcode::AsrRI, {rhsReg, rhsReg, MOperand::immOp(magic.shift)}});
    }

    // lsr dst, tmp, #63 (extract sign bit into dst as temp)
    expansion.push_back(MInstr{MOpcode::LsrRI, {dst, rhsReg, MOperand::immOp(63)}});

    // add dst, tmp, dst (round toward zero)
    expansion.push_back(MInstr{MOpcode::AddRRR, {dst, rhsReg, dst}});

    // Replace the SDivRRR with the expansion
    instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(idx));
    instrs.insert(
        instrs.begin() + static_cast<std::ptrdiff_t>(idx), expansion.begin(), expansion.end());

    ++stats.strengthReductions;
    return true;
}

bool tryRemainderFusion(std::vector<MInstr> &instrs,
                        std::size_t idx,
                        const RegConstMap &knownConsts,
                        PeepholeStats &stats)
{
    // Match the pattern: [SU]DivRRR tmp, lhs, rhs; MSubRRRR dst, tmp, rhs, lhs
    // where rhs is a known power-of-2 constant.
    //
    // For UREM by power-of-2 N:
    //   Replace with: AndRI dst, lhs, #(N-1)
    //
    // For SREM by power-of-2 N:
    //   The SDIV is handled by trySDivStrengthReduction (which expands it to shifts).
    //   If we get here, SDIV was NOT yet strength-reduced, so we do it inline:
    //   Replace SDIV+MSUB with:
    //     asr  tmp, lhs, #63
    //     and  tmp, tmp, #(N-1)      ; mask = (2^k-1) if negative, 0 if positive
    //     add  tmp, lhs, tmp         ; bias lhs toward zero
    //     and  tmp, tmp, #~(N-1)     ; clear lower k bits (round down to multiple of N)
    //     sub  dst, lhs, tmp         ; remainder = lhs - rounded_down
    //   But this is complex and may not always fit in valid logical immediates.
    //   For now, only handle UREM by power-of-2.

    if (idx + 1 >= instrs.size())
        return false;

    auto &divInstr = instrs[idx];
    auto &msubInstr = instrs[idx + 1];

    // Check for [SU]DivRRR
    const bool isUnsigned = (divInstr.opc == MOpcode::UDivRRR);
    if (divInstr.opc != MOpcode::UDivRRR && divInstr.opc != MOpcode::SDivRRR)
        return false;
    if (divInstr.ops.size() != 3)
        return false;

    // Check for MSubRRRR
    if (msubInstr.opc != MOpcode::MSubRRRR || msubInstr.ops.size() != 4)
        return false;

    // Verify the pattern: msub dst, divDst, rhs, lhs
    // MSubRRRR ops: [dst, mul1, mul2, sub] => dst = sub - mul1*mul2
    const MOperand &divDst = divInstr.ops[0];
    const MOperand &divLhs = divInstr.ops[1];
    const MOperand &divRhs = divInstr.ops[2];

    // msub's mul1 must be the div result
    if (!samePhysReg(msubInstr.ops[1], divDst))
        return false;

    // msub's mul2 must be the divisor
    if (!samePhysReg(msubInstr.ops[2], divRhs))
        return false;

    // msub's sub operand must be the dividend
    if (!samePhysReg(msubInstr.ops[3], divLhs))
        return false;

    // Get the constant divisor
    auto rhsConst = getConstValue(divRhs, knownConsts);
    if (!rhsConst || *rhsConst <= 1)
        return false;

    const long long divisor = *rhsConst;
    const int log = log2IfPowerOf2(divisor);

    if (log < 1 || log > 63)
        return false;

    const long long mask = divisor - 1; // e.g., 8-1 = 7 = 0b111

    // Verify the div result register is not used after the msub
    for (std::size_t i = idx + 2; i < instrs.size(); ++i)
    {
        if (usesReg(instrs[i], divDst))
            return false;
        if (definesReg(instrs[i], divDst))
            break;
    }

    if (isUnsigned)
    {
        // UREM by power-of-2: x % N == x & (N-1)
        // Verify mask is a valid AArch64 logical immediate
        if (!isLogicalImmediate(static_cast<uint64_t>(mask)))
            return false;

        const MOperand remDst = msubInstr.ops[0];

        // Replace both instructions with: and dst, lhs, #(N-1)
        instrs[idx] = MInstr{MOpcode::AndRI, {remDst, divLhs, MOperand::immOp(mask)}};
        instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(idx + 1));

        ++stats.strengthReductions;
        return true;
    }

    // SREM by power-of-2 for signed values.
    // C/IL semantics: remainder has the same sign as the dividend.
    //
    // Optimized sequence:
    //   negs tmp, lhs             ; negate (but we don't have negs)
    //   Actually, the cleanest approach for SREM by 2^k:
    //
    //   asr  tmp, lhs, #63        ; sign mask: -1 if negative, 0 if positive
    //   lsr  tmp, tmp, #(64-k)    ; (2^k - 1) if negative, 0 if positive
    //   add  tmp, lhs, tmp        ; biased value
    //   and  tmp, tmp, #-(2^k)    ; round down to multiple of 2^k (clear low k bits)
    //   sub  dst, lhs, tmp        ; remainder = lhs - rounded_down
    //
    // #-(2^k) as a logical immediate: this is ~(2^k - 1). For k=1 that's
    // 0xFFFFFFFFFFFFFFFE, which is a valid logical immediate (alternating pattern).

    // Check if both masks are valid logical immediates
    const auto negMask = static_cast<uint64_t>(~mask); // ~(N-1) = -(N)
    if (!isLogicalImmediate(negMask))
        return false;

    // We need a temp register. Use the div's destination register.
    if (!isPhysReg(divDst))
        return false;

    const MOperand tmp = divDst;
    const MOperand remDst = msubInstr.ops[0];

    std::vector<MInstr> expansion;

    // asr tmp, lhs, #63
    expansion.push_back(MInstr{MOpcode::AsrRI, {tmp, divLhs, MOperand::immOp(63)}});

    // lsr tmp, tmp, #(64-k)
    expansion.push_back(MInstr{MOpcode::LsrRI, {tmp, tmp, MOperand::immOp(64 - log)}});

    // add tmp, lhs, tmp
    expansion.push_back(MInstr{MOpcode::AddRRR, {tmp, divLhs, tmp}});

    // and tmp, tmp, #-(2^k)   (clear low k bits to round down)
    expansion.push_back(
        MInstr{MOpcode::AndRI, {tmp, tmp, MOperand::immOp(static_cast<long long>(negMask))}});

    // sub dst, lhs, tmp       (remainder = lhs - rounded_down)
    expansion.push_back(MInstr{MOpcode::SubRRR, {remDst, divLhs, tmp}});

    // Replace both instructions with the expansion
    instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(idx),
                 instrs.begin() + static_cast<std::ptrdiff_t>(idx + 2));
    instrs.insert(
        instrs.begin() + static_cast<std::ptrdiff_t>(idx), expansion.begin(), expansion.end());

    ++stats.strengthReductions;
    return true;
}

} // namespace viper::codegen::aarch64::peephole
