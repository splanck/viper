//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/Peephole.cpp
// Purpose: Implement conservative peephole optimizations over Machine IR for
//          the AArch64 backend.
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
/// @brief Peephole optimization pass for the AArch64 code generator.
/// @details Implements local rewrites that eliminate redundant moves and
///          fold consecutive register-to-register operations. The patterns
///          implemented are conservative and safe to apply after register
///          allocation.

#include "Peephole.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::aarch64
{
namespace
{

/// @brief Check if an operand is a physical register.
///
/// @param op Operand to check.
/// @return true if the operand is a physical register.
[[nodiscard]] bool isPhysReg(const MOperand &op) noexcept
{
    return op.kind == MOperand::Kind::Reg && op.reg.isPhys;
}

/// @brief Check if two register operands refer to the same physical register.
///
/// @param a First operand.
/// @param b Second operand.
/// @return true if both are physical registers with the same class and ID.
[[nodiscard]] bool samePhysReg(const MOperand &a, const MOperand &b) noexcept
{
    if (!isPhysReg(a) || !isPhysReg(b))
        return false;
    return a.reg.cls == b.reg.cls && a.reg.idOrPhys == b.reg.idOrPhys;
}

/// @brief Check if an instruction is an identity move (mov r, r).
///
/// @param instr Instruction to check.
/// @return true if the instruction is MovRR with identical source and destination.
[[nodiscard]] bool isIdentityMovRR(const MInstr &instr) noexcept
{
    if (instr.opc != MOpcode::MovRR)
        return false;
    if (instr.ops.size() != 2)
        return false;
    return samePhysReg(instr.ops[0], instr.ops[1]);
}

/// @brief Check if an instruction is an identity FPR move (fmov d, d).
///
/// @param instr Instruction to check.
/// @return true if the instruction is FMovRR with identical source and destination.
[[nodiscard]] bool isIdentityFMovRR(const MInstr &instr) noexcept
{
    if (instr.opc != MOpcode::FMovRR)
        return false;
    if (instr.ops.size() != 2)
        return false;
    return samePhysReg(instr.ops[0], instr.ops[1]);
}

/// @brief Check if an instruction defines a given physical register.
///
/// @param instr Instruction to check.
/// @param reg Register to look for.
/// @return true if the instruction writes to the given register.
[[nodiscard]] bool definesReg(const MInstr &instr, const MOperand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;

    // Most AArch64 instructions have the destination as the first operand.
    // Check common patterns.
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::Cset:
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::AddFpImm:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::FRintN:
        case MOpcode::MSubRRRR:
            if (!instr.ops.empty() && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        // Instructions that don't define registers
        case MOpcode::CmpRR:
        case MOpcode::CmpRI:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Bl:
        case MOpcode::Ret:
        case MOpcode::Cbz:
        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprSpImm:
        case MOpcode::SubSpImm:
        case MOpcode::AddSpImm:
            break;
    }
    return false;
}

/// @brief Check if an instruction uses a given physical register as a source.
///
/// @param instr Instruction to check.
/// @param reg Register to look for.
/// @return true if the instruction reads from the given register.
[[nodiscard]] bool usesReg(const MInstr &instr, const MOperand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;

    // Check source operands (typically starting from index 1 for most ops)
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::FMovRR:
            // dst, src - check src (index 1)
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
            // dst, lhs, rhs - check lhs and rhs (indices 1, 2)
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            if (instr.ops.size() >= 3 && samePhysReg(instr.ops[2], reg))
                return true;
            break;

        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
            // dst, src, imm - check src (index 1)
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::CmpRR:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
            // lhs, rhs - check both
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::CmpRI:
            // src, imm - check src
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
            // src, offset - check src
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
            // src, base, offset - check src and base
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
            // dst, base, offset - check base
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::Cbz:
            // reg, label - check reg
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
            // dst, src - check src
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::MSubRRRR:
            // dst, mul1, mul2, sub - check mul1, mul2, sub
            for (std::size_t i = 1; i < instr.ops.size() && i <= 3; ++i)
            {
                if (samePhysReg(instr.ops[i], reg))
                    return true;
            }
            break;

        case MOpcode::AddPageOff:
            // dst, base, label - check base
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        default:
            break;
    }
    return false;
}

/// @brief Check if a register is an argument-passing register (x0-x7).
///
/// @param reg Register to check.
/// @return true if the register is one of x0-x7.
[[nodiscard]] bool isArgReg(const MOperand &reg) noexcept
{
    if (!isPhysReg(reg) || reg.reg.cls != RegClass::GPR)
        return false;
    const auto pr = static_cast<PhysReg>(reg.reg.idOrPhys);
    return pr >= PhysReg::X0 && pr <= PhysReg::X7;
}

/// @brief Check if an operand is an immediate with a given value.
[[nodiscard]] bool isImmValue(const MOperand &op, long long value) noexcept
{
    return op.kind == MOperand::Kind::Imm && op.imm == value;
}

/// @brief Rewrite cmp reg, #0 to tst reg, reg (same flags, smaller encoding).
///
/// @param instr Instruction to check and potentially rewrite.
/// @param stats Statistics to update.
/// @return true if the instruction was rewritten.
[[nodiscard]] bool tryCmpZeroToTst(MInstr &instr, PeepholeStats &stats)
{
    if (instr.opc != MOpcode::CmpRI)
        return false;
    if (instr.ops.size() != 2)
        return false;
    if (!isPhysReg(instr.ops[0]) || !isImmValue(instr.ops[1], 0))
        return false;

    // Rewrite: cmp xN, #0 -> tst xN, xN
    instr.opc = MOpcode::TstRR;
    instr.ops[1] = instr.ops[0]; // second operand = same register
    ++stats.cmpZeroToTst;
    return true;
}

/// @brief Check if a power of 2 and return the log2, or -1 if not.
[[nodiscard]] int log2IfPowerOf2(long long value) noexcept
{
    if (value <= 0)
        return -1;
    if ((value & (value - 1)) != 0)
        return -1; // not a power of 2
    int log = 0;
    while ((1LL << log) < value)
        ++log;
    return log;
}

/// @brief Rewrite arithmetic identity operations and apply strength reduction.
///
/// Patterns:
/// - add xN, xM, #0 -> mov xN, xM (or remove if xN == xM)
/// - sub xN, xM, #0 -> mov xN, xM
/// - lsl/lsr/asr xN, xM, #0 -> mov xN, xM
///
/// @param instr Instruction to check and potentially rewrite.
/// @param stats Statistics to update.
/// @return true if the instruction was rewritten.
[[nodiscard]] bool tryArithmeticIdentity(MInstr &instr, PeepholeStats &stats)
{
    switch (instr.opc)
    {
        case MOpcode::AddRI:
        case MOpcode::SubRI:
            // add/sub xN, xM, #0 -> mov xN, xM
            if (instr.ops.size() == 3 && isImmValue(instr.ops[2], 0))
            {
                instr.opc = MOpcode::MovRR;
                instr.ops.pop_back(); // remove immediate
                ++stats.arithmeticIdentities;
                return true;
            }
            break;

        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
            // shift by 0 -> mov
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

/// @brief Map of registers to their known constant values from MovRI.
using RegConstMap = std::unordered_map<uint16_t, long long>;

/// @brief Update register constant tracking based on an instruction.
///
/// @param instr Instruction to analyze.
/// @param knownConsts Map of registers to their constant values.
void updateKnownConsts(const MInstr &instr, RegConstMap &knownConsts)
{
    // MovRI loads a constant into a register
    if (instr.opc == MOpcode::MovRI && instr.ops.size() == 2 && isPhysReg(instr.ops[0]) &&
        instr.ops[1].kind == MOperand::Kind::Imm)
    {
        knownConsts[instr.ops[0].reg.idOrPhys] = instr.ops[1].imm;
        return;
    }

    // Any other instruction that defines a register invalidates the constant
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::Cset:
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::AddFpImm:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::MSubRRRR:
            if (!instr.ops.empty() && isPhysReg(instr.ops[0]))
                knownConsts.erase(instr.ops[0].reg.idOrPhys);
            break;
        default:
            break;
    }

    // Calls invalidate all caller-saved registers (x0-x18)
    if (instr.opc == MOpcode::Bl)
    {
        for (uint16_t i = 0; i <= 18; ++i)
            knownConsts.erase(i);
    }
}

/// @brief Get constant value for a register if known.
[[nodiscard]] std::optional<long long> getConstValue(const MOperand &reg,
                                                     const RegConstMap &knownConsts)
{
    if (!isPhysReg(reg) || reg.reg.cls != RegClass::GPR)
        return std::nullopt;
    auto it = knownConsts.find(reg.reg.idOrPhys);
    if (it != knownConsts.end())
        return it->second;
    return std::nullopt;
}

/// @brief Apply strength reduction: mul by power of 2 -> shift left.
///
/// Patterns:
/// - mul xN, xM, xK where xK is a power of 2 -> lsl xN, xM, #log2(xK)
/// - mul xN, xK, xM where xK is a power of 2 -> lsl xN, xM, #log2(xK)
///
/// @param instr Instruction to check and potentially rewrite.
/// @param knownConsts Map of registers to known constant values.
/// @param stats Statistics to update.
/// @return true if reduction was applied.
[[nodiscard]] bool tryStrengthReduction(MInstr &instr,
                                        const RegConstMap &knownConsts,
                                        PeepholeStats &stats)
{
    if (instr.opc != MOpcode::MulRRR)
        return false;
    if (instr.ops.size() != 3)
        return false;

    // Check if either operand (lhs=ops[1] or rhs=ops[2]) is a known power-of-2 constant
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

    // Rewrite: mul dst, xM, xK -> lsl dst, xM, #shift
    instr.opc = MOpcode::LslRI;
    instr.ops[1] = otherOperand;
    instr.ops[2] = MOperand::immOp(shiftAmount);
    ++stats.strengthReductions;
    return true;
}

/// @brief Check if an instruction is an unconditional branch to a specific label.
[[nodiscard]] bool isBranchTo(const MInstr &instr, const std::string &label) noexcept
{
    if (instr.opc != MOpcode::Br)
        return false;
    if (instr.ops.empty() || instr.ops[0].kind != MOperand::Kind::Label)
        return false;
    return instr.ops[0].label == label;
}

/// @brief Rewrite FP arithmetic identity operations.
///
/// Patterns:
/// - fadd dN, dM, #0.0 -> fmov dN, dM (if we had FAddRI)
/// - fsub dN, dM, #0.0 -> fmov dN, dM (if we had FSubRI)
/// - fmul dN, dM, #1.0 -> fmov dN, dM (if we had FMulRI)
///
/// Note: Currently the ARM64 backend doesn't have FP immediate forms for
/// arithmetic ops, so this is a placeholder for future enhancement.
///
/// @param instr Instruction to check.
/// @param stats Statistics to update.
/// @return true if the instruction was rewritten.
[[nodiscard]] bool tryFPArithmeticIdentity([[maybe_unused]] MInstr &instr,
                                           [[maybe_unused]] PeepholeStats &stats)
{
    // Currently no FP arithmetic with immediate forms in the MIR.
    // FAddRRR, FSubRRR, FMulRRR all take register operands.
    // Would need to track fmov dN, #const patterns for this optimization.
    return false;
}

/// @brief Try to fold consecutive moves: mov r1, r2; mov r3, r1 -> mov r3, r2
///
/// This optimization is only safe when r1 is not used after the second move
/// within the same basic block, and when there are no intervening calls that
/// might implicitly use argument registers.
///
/// @param instrs Vector of instructions in a basic block.
/// @param idx Index of the first instruction to check.
/// @return true if a fold was performed.
[[nodiscard]] bool tryFoldConsecutiveMoves(std::vector<MInstr> &instrs,
                                           std::size_t idx,
                                           PeepholeStats &stats)
{
    if (idx + 1 >= instrs.size())
        return false;

    MInstr &first = instrs[idx];
    MInstr &second = instrs[idx + 1];

    // Check for: mov r1, r2; mov r3, r1
    const bool firstIsMovRR = (first.opc == MOpcode::MovRR && first.ops.size() == 2);
    const bool secondIsMovRR = (second.opc == MOpcode::MovRR && second.ops.size() == 2);

    if (!firstIsMovRR || !secondIsMovRR)
        return false;

    // first: dst=r1, src=r2
    // second: dst=r3, src=r1
    // Check if second.src == first.dst
    if (!samePhysReg(second.ops[1], first.ops[0]))
        return false;

    // Don't fold if the intermediate register is an argument register and
    // there's a call instruction nearby. Call instructions implicitly use
    // argument registers, which our simple liveness check doesn't detect.
    const MOperand &r1 = first.ops[0];
    if (isArgReg(r1))
    {
        // Check for call instructions after the second move
        for (std::size_t i = idx + 2; i < instrs.size(); ++i)
        {
            if (instrs[i].opc == MOpcode::Bl)
                return false; // Call instruction found, don't fold
            if (definesReg(instrs[i], r1))
                break; // r1 is redefined before any call, safe to check further
        }
    }

    // Check that r1 is not used after second in this block
    for (std::size_t i = idx + 2; i < instrs.size(); ++i)
    {
        if (usesReg(instrs[i], r1))
            return false; // r1 is still live, can't fold
        if (definesReg(instrs[i], r1))
            break; // r1 is redefined, safe to fold
    }

    // Perform the fold: second becomes mov r3, r2
    const MOperand originalSrc = first.ops[1]; // Save the original source
    second.ops[1] = originalSrc;               // second.src = first.src
    first.ops[0] = first.ops[1];               // Make first identity (mov r2, r2)
    ++stats.consecutiveMovsFolded;
    return true;
}

/// @brief Remove instructions marked for deletion from a basic block.
///
/// @param instrs Vector of instructions to filter.
/// @param toRemove Set of indices to remove.
void removeMarkedInstructions(std::vector<MInstr> &instrs, const std::vector<bool> &toRemove)
{
    std::size_t writeIdx = 0;
    for (std::size_t readIdx = 0; readIdx < instrs.size(); ++readIdx)
    {
        if (!toRemove[readIdx])
        {
            if (writeIdx != readIdx)
                instrs[writeIdx] = std::move(instrs[readIdx]);
            ++writeIdx;
        }
    }
    instrs.resize(writeIdx);
}

/// @brief Get a unique key for a physical register (for use in maps).
[[nodiscard]] uint32_t regKey(const MOperand &op) noexcept
{
    if (op.kind != MOperand::Kind::Reg || !op.reg.isPhys)
        return UINT32_MAX;
    // Combine class and physical register ID into a unique key
    return (static_cast<uint32_t>(op.reg.cls) << 16) | op.reg.idOrPhys;
}

/// @brief Classify an operand as use, def, or both.
///
/// Returns a pair (isUse, isDef) for the operand at the given index.
[[nodiscard]] std::pair<bool, bool> classifyOperand(const MInstr &instr, std::size_t idx) noexcept
{
    // Most AArch64 instructions have dst at index 0 (def), sources at 1+ (use)
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
            // mov dst, src
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);

        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
            // op dst, lhs, rhs
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);

        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
            // op dst, src, imm
            if (idx == 0)
                return {false, true};
            if (idx == 1)
                return {true, false};
            return {false, false}; // imm

        case MOpcode::CmpRR:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
            // cmp lhs, rhs (no def, both uses)
            return {true, false};

        case MOpcode::CmpRI:
            // cmp src, imm
            return idx == 0 ? std::make_pair(true, false) : std::make_pair(false, false);

        case MOpcode::Cset:
            // cset dst, cond
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(false, false);

        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
            // ldr dst, [fp, #imm]
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(false, false);

        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
            // ldr dst, [base, #imm]
            if (idx == 0)
                return {false, true};
            if (idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
            // str src, [fp, #imm]
            return idx == 0 ? std::make_pair(true, false) : std::make_pair(false, false);

        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
            // str src, [base, #imm]
            if (idx == 0)
                return {true, false};
            if (idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::FRintN:
            // cvt dst, src
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);

        case MOpcode::MSubRRRR:
            // msub dst, m1, m2, sub
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);

        case MOpcode::AdrPage:
            // adr dst, label
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(false, false);

        case MOpcode::AddPageOff:
            // add dst, base, label
            if (idx == 0)
                return {false, true};
            if (idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::Cbz:
            // cbz reg, label
            return idx == 0 ? std::make_pair(true, false) : std::make_pair(false, false);

        case MOpcode::AddFpImm:
            // add dst, fp, imm
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(false, false);

        default:
            // Conservative: assume first operand is def, rest are uses
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);
    }
}

/// @brief Perform copy propagation within a basic block.
///
/// Tracks register copy relationships and propagates source registers through
/// the block. When a register's value comes from a copy, we replace uses with
/// the original source, potentially eliminating intermediate copies.
///
/// Example:
///   mov x1, x0    ; x1 = x0
///   add x2, x1, #1  ; becomes: add x2, x0, #1
///   mov x3, x1    ; becomes: mov x3, x0 (then maybe identity if x1 not used)
///
/// @param instrs Instructions in the basic block.
/// @param stats Statistics to update.
/// @return Number of copies propagated.
std::size_t propagateCopies(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    // Map from register key to its "origin" register (the source of the copy chain)
    std::unordered_map<uint32_t, MOperand> copyOrigin;
    std::size_t propagated = 0;

    // Helper to invalidate all copies that depend on a register as their origin
    auto invalidateDependents = [&copyOrigin](uint32_t originKey)
    {
        // Collect keys to erase (can't erase while iterating)
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
        // Skip non-register instructions
        if (instr.opc == MOpcode::Br || instr.opc == MOpcode::BCond || instr.opc == MOpcode::Ret ||
            instr.opc == MOpcode::Bl)
        {
            // Calls clobber caller-saved registers, invalidate all copy info
            if (instr.opc == MOpcode::Bl)
                copyOrigin.clear();
            continue;
        }

        // For MovRR (register-to-register move), track the copy relationship
        if (instr.opc == MOpcode::MovRR && instr.ops.size() == 2 && isPhysReg(instr.ops[0]) &&
            isPhysReg(instr.ops[1]))
        {
            const MOperand &dst = instr.ops[0];
            const MOperand &src = instr.ops[1];
            uint32_t dstKey = regKey(dst);

            // First invalidate any copies that depend on dst as their origin
            // (since dst is being redefined)
            invalidateDependents(dstKey);
            copyOrigin.erase(dstKey);

            // Find the origin of the source (follow the copy chain)
            uint32_t srcKey = regKey(src);
            MOperand origin = src;
            auto it = copyOrigin.find(srcKey);
            if (it != copyOrigin.end())
                origin = it->second;

            // If dst != origin, record the copy relationship
            if (!samePhysReg(dst, origin))
            {
                copyOrigin[dstKey] = origin;

                // Update the mov to use the origin directly
                if (!samePhysReg(src, origin))
                {
                    instr.ops[1] = origin;
                    ++propagated;
                }
            }
            continue;
        }

        // For FMovRR (FP register move), same logic
        if (instr.opc == MOpcode::FMovRR && instr.ops.size() == 2 && isPhysReg(instr.ops[0]) &&
            isPhysReg(instr.ops[1]))
        {
            const MOperand &dst = instr.ops[0];
            const MOperand &src = instr.ops[1];
            uint32_t dstKey = regKey(dst);

            invalidateDependents(dstKey);
            copyOrigin.erase(dstKey);

            uint32_t srcKey = regKey(src);
            MOperand origin = src;
            auto it = copyOrigin.find(srcKey);
            if (it != copyOrigin.end())
                origin = it->second;

            if (!samePhysReg(dst, origin))
            {
                copyOrigin[dstKey] = origin;

                if (!samePhysReg(src, origin))
                {
                    instr.ops[1] = origin;
                    ++propagated;
                }
            }
            continue;
        }

        // For other instructions, propagate copies in source operands
        // and invalidate copy info for defined registers

        // First pass: collect definitions to invalidate
        for (std::size_t i = 0; i < instr.ops.size(); ++i)
        {
            const auto &op = instr.ops[i];
            if (!isPhysReg(op))
                continue;

            auto [isUse, isDef] = classifyOperand(instr, i);
            if (isDef)
            {
                // This register is redefined, invalidate its copy info
                // and all copies that depend on it
                uint32_t key = regKey(op);
                invalidateDependents(key);
                copyOrigin.erase(key);
            }
        }

        // Second pass: propagate copies in uses
        for (std::size_t i = 0; i < instr.ops.size(); ++i)
        {
            auto &op = instr.ops[i];
            if (!isPhysReg(op))
                continue;

            auto [isUse, isDef] = classifyOperand(instr, i);
            if (isUse && !isDef)
            {
                // Try to replace with origin
                uint32_t key = regKey(op);
                auto it = copyOrigin.find(key);
                if (it != copyOrigin.end() && !samePhysReg(op, it->second))
                {
                    op = it->second;
                    ++propagated;
                }
            }
        }
    }

    stats.copiesPropagated += static_cast<int>(propagated);
    return propagated;
}

/// @brief Check if an instruction has side effects and cannot be removed.
///
/// Instructions with side effects include stores, calls, branches, and
/// control flow instructions.
[[nodiscard]] bool hasSideEffects(const MInstr &instr) noexcept
{
    switch (instr.opc)
    {
        // Store instructions - write to memory
        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprSpImm:
            return true;

        // Call instructions - may have arbitrary effects
        case MOpcode::Bl:
            return true;

        // Branch and control flow - affect program flow
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Ret:
        case MOpcode::Cbz:
            return true;

        // Stack manipulation
        case MOpcode::SubSpImm:
        case MOpcode::AddSpImm:
            return true;

        // Comparison instructions set flags that may be used by branches
        case MOpcode::CmpRR:
        case MOpcode::CmpRI:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
            return true;

        // Load instructions - their results may be used in other basic blocks
        // Since we don't have cross-block liveness, treat all loads as having side effects
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
            return true;

        // Address-loading instructions (adrp/add for PC-relative addressing)
        // These are typically used to load string constants for function calls.
        // Without proper def-use chains, we can't safely remove these.
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::AddFpImm:
            return true;

        // Mov instructions to argument/return registers (x0-x7, v0-v7) may be setting up
        // call arguments or return values. Without proper use-def analysis across calls,
        // we treat these as having potential side effects.
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        {
            if (instr.ops.empty())
                return false;
            const auto &dst = instr.ops[0];
            if (dst.kind != MOperand::Kind::Reg || !dst.reg.isPhys)
                return false;
            auto pr = static_cast<PhysReg>(dst.reg.idOrPhys);
            // x0-x7 are argument/return registers
            if (pr >= PhysReg::X0 && pr <= PhysReg::X7)
                return true;
            // v0-v7 are FP argument/return registers
            if (pr >= PhysReg::V0 && pr <= PhysReg::V7)
                return true;
            return false;
        }

        // All other instructions are pure computations
        default:
            return false;
    }
}

/// @brief Get the physical register defined by an instruction, if any.
///
/// @param instr Instruction to analyze.
/// @return The defined register operand, or nullopt if none.
[[nodiscard]] std::optional<MOperand> getDefinedReg(const MInstr &instr) noexcept
{
    // Most instructions define their first operand
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::Cset:
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::AddFpImm:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::FRintN:
        case MOpcode::MSubRRRR:
            if (!instr.ops.empty() && isPhysReg(instr.ops[0]))
                return instr.ops[0];
            break;

        default:
            break;
    }
    return std::nullopt;
}

/// @brief Perform dead code elimination within a basic block.
///
/// Uses backward liveness analysis to remove instructions whose results
/// are never used. Only removes pure computations (no side effects).
///
/// @param instrs Instructions in the basic block.
/// @param stats Statistics to update.
/// @return Number of dead instructions removed.
std::size_t removeDeadInstructions(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    if (instrs.empty())
        return 0;

    // Set of registers that are live (used by subsequent instructions)
    std::unordered_set<uint32_t> liveRegs;

    // Mark all argument registers as live at block exit (conservative)
    // They may be used by calls or the return value
    for (int i = 0; i <= 7; ++i)
    {
        liveRegs.insert((static_cast<uint32_t>(RegClass::GPR) << 16) |
                        static_cast<uint32_t>(PhysReg::X0) + i);
    }

    // Also mark FP argument registers as live
    for (int i = 0; i <= 7; ++i)
    {
        liveRegs.insert((static_cast<uint32_t>(RegClass::FPR) << 16) |
                        static_cast<uint32_t>(PhysReg::V0) + i);
    }

    // Backward scan to compute liveness and mark dead instructions
    std::vector<bool> toRemove(instrs.size(), false);
    std::size_t removed = 0;

    for (std::size_t i = instrs.size(); i > 0; --i)
    {
        const std::size_t idx = i - 1;
        const auto &instr = instrs[idx];

        // Instructions with side effects are always live
        if (hasSideEffects(instr))
        {
            // Mark all source operands as live
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

        // Check if the instruction's result is used
        auto defReg = getDefinedReg(instr);
        if (defReg)
        {
            uint32_t key = regKey(*defReg);
            if (liveRegs.find(key) == liveRegs.end())
            {
                // Result is not used - mark for removal
                toRemove[idx] = true;
                ++removed;
                continue;
            }

            // Result is used - remove from live set (since we're defining it)
            liveRegs.erase(key);
        }

        // Mark all source operands as live
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

    // Remove marked instructions
    if (removed > 0)
    {
        removeMarkedInstructions(instrs, toRemove);
        stats.deadInstructionsRemoved += static_cast<int>(removed);
    }

    return removed;
}

/// @brief Check if a block is a cold block (trap handler, error block).
///
/// Cold blocks are unlikely to be executed and should be placed at the end
/// of the function to improve instruction cache locality.
[[nodiscard]] bool isColdBlock(const MBasicBlock &block) noexcept
{
    // Blocks with "trap" in the name are error handlers
    if (block.name.find("trap") != std::string::npos)
        return true;
    if (block.name.find("error") != std::string::npos)
        return true;
    if (block.name.find("panic") != std::string::npos)
        return true;

    // Blocks that only call trap functions are cold
    if (block.instrs.size() == 1)
    {
        const auto &instr = block.instrs[0];
        if (instr.opc == MOpcode::Bl)
        {
            if (!instr.ops.empty() && instr.ops[0].kind == MOperand::Kind::Label)
            {
                const auto &label = instr.ops[0].label;
                if (label.find("trap") != std::string::npos ||
                    label.find("panic") != std::string::npos)
                    return true;
            }
        }
    }
    return false;
}

/// @brief Reorder blocks for better code layout.
///
/// This function reorders blocks using a conservative heuristic:
/// 1. Keep all hot blocks in their original order
/// 2. Move cold blocks (trap handlers, error blocks) to the end
///
/// This is conservative to avoid disrupting carefully crafted block layouts
/// while still improving icache locality by pushing error paths to the end.
///
/// @param fn Function to reorder.
/// @return Number of blocks moved.
std::size_t reorderBlocks(MFunction &fn)
{
    if (fn.blocks.size() <= 2)
        return 0; // Nothing to reorder

    // First pass: identify cold blocks by index
    std::vector<std::size_t> coldIndices;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
    {
        if (isColdBlock(fn.blocks[i]))
            coldIndices.push_back(i);
    }

    // If no cold blocks, nothing to do
    if (coldIndices.empty())
        return 0;

    // Build new block order: hot blocks first, then cold blocks
    std::vector<MBasicBlock> reordered;
    reordered.reserve(fn.blocks.size());

    // Add hot blocks in original order
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
    {
        bool isCold = std::find(coldIndices.begin(), coldIndices.end(), i) != coldIndices.end();
        if (!isCold)
            reordered.push_back(std::move(fn.blocks[i]));
    }

    // Add cold blocks at the end
    for (std::size_t idx : coldIndices)
        reordered.push_back(std::move(fn.blocks[idx]));

    fn.blocks = std::move(reordered);
    return coldIndices.size();
}

} // namespace

PeepholeStats runPeephole(MFunction &fn)
{
    PeepholeStats stats;

    // Pass 0: Reorder blocks for better code layout
    stats.blocksReordered = static_cast<int>(reorderBlocks(fn));

    for (auto &block : fn.blocks)
    {
        auto &instrs = block.instrs;
        if (instrs.empty())
            continue;

        // Pass 1: Build register constant map and apply rewrites
        RegConstMap knownConsts;
        for (auto &instr : instrs)
        {
            // Track constants loaded via MovRI
            updateKnownConsts(instr, knownConsts);

            // Try cmp reg, #0 -> tst reg, reg
            if (tryCmpZeroToTst(instr, stats))
                continue;

            // Try arithmetic identity elimination (add #0, sub #0, shift #0)
            if (tryArithmeticIdentity(instr, stats))
                continue;

            // Try strength reduction (mul power-of-2 -> shift)
            (void)tryStrengthReduction(instr, knownConsts, stats);
        }

        // Pass 1.5: Copy propagation - replace uses with original sources
        // TODO: Disabled pending investigation of correctness issues
        // propagateCopies(instrs, stats);

        // Pass 2: Try to fold consecutive moves
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
        {
            (void)tryFoldConsecutiveMoves(instrs, i, stats);
        }

        // Pass 3: Mark identity moves for removal
        std::vector<bool> toRemove(instrs.size(), false);

        for (std::size_t i = 0; i < instrs.size(); ++i)
        {
            if (isIdentityMovRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityMovesRemoved;
            }
            else if (isIdentityFMovRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityFMovesRemoved;
            }
        }

        // Pass 4: Remove marked instructions
        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool v) { return v; }))
        {
            removeMarkedInstructions(instrs, toRemove);
        }

        // Pass 4.5: Dead code elimination - remove instructions with unused results
        removeDeadInstructions(instrs, stats);
    }

    // Pass 5: Remove branches to the immediately following block
    // This must be done after per-block passes since it looks at adjacent blocks.
    for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi)
    {
        auto &block = fn.blocks[bi];
        const auto &nextBlock = fn.blocks[bi + 1];

        if (block.instrs.empty())
            continue;

        // Check if the last instruction is an unconditional branch to the next block
        auto &lastInstr = block.instrs.back();
        if (isBranchTo(lastInstr, nextBlock.name))
        {
            block.instrs.pop_back();
            ++stats.branchesToNextRemoved;
        }
    }

    return stats;
}

} // namespace viper::codegen::aarch64
