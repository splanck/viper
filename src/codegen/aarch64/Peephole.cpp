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

/// @brief Apply strength reduction: mul by power of 2 -> shift left.
///
/// Note: This only works for MulRRR where one operand is a constant loaded
/// into a register. For now, we skip this since we don't have MulRI opcode.
/// Future: could track mov rN, #const patterns.
///
/// @param instr Instruction to check.
/// @param stats Statistics to update.
/// @return true if reduction was applied.
[[nodiscard]] bool tryStrengthReduction([[maybe_unused]] MInstr &instr,
                                        [[maybe_unused]] PeepholeStats &stats)
{
    // Currently MulRRR doesn't have an immediate form, so we can't easily
    // detect multiply by constant without tracking prior mov instructions.
    // This would require a more sophisticated analysis.
    return false;
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

} // namespace

PeepholeStats runPeephole(MFunction &fn)
{
    PeepholeStats stats;

    for (auto &block : fn.blocks)
    {
        auto &instrs = block.instrs;
        if (instrs.empty())
            continue;

        // Pass 1: Apply instruction rewrites (cmp #0 -> tst, arithmetic identities)
        for (auto &instr : instrs)
        {
            // Try cmp reg, #0 -> tst reg, reg
            if (tryCmpZeroToTst(instr, stats))
                continue;

            // Try arithmetic identity elimination (add #0, sub #0, shift #0)
            if (tryArithmeticIdentity(instr, stats))
                continue;

            // Try strength reduction (mul power-of-2 -> shift)
            (void)tryStrengthReduction(instr, stats);
        }

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
