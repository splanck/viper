//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Peephole.cpp
// Purpose: Implement conservative peephole optimisations over Machine IR for
//          the x86-64 backend.
// Key invariants: Rewrites preserve instruction ordering and only substitute
//                 encodings that are provably equivalent under the Phase A
//                 Machine IR conventions.
// Ownership/Lifetime: Mutates Machine IR graphs owned by the caller without
//                     retaining references to transient operands.
// Links: docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Peephole optimisation pass for the x86-64 code generator.
/// @details Documents the local rewrites that fold `mov`-zero and `cmp`-zero
///          patterns into smaller `xor`/`test` sequences.  The file concentrates
///          the pattern recognisers and mutation helpers so later extensions can
///          add new folds while sharing the same descriptive comments and
///          invariants.

#include "Peephole.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace viper::codegen::x64
{
namespace
{

/// @brief Statistics tracking for peephole optimizations.
struct PeepholeStats
{
    std::size_t movZeroToXor{0};
    std::size_t cmpZeroToTest{0};
    std::size_t arithmeticIdentities{0};
    std::size_t strengthReductions{0};
    std::size_t identityMovesRemoved{0};
    std::size_t consecutiveMovsFolded{0};
    std::size_t branchesToNextRemoved{0};
    std::size_t deadCodeEliminated{0};
    std::size_t coldBlocksMoved{0};
};
/// @brief Test whether an operand is the immediate integer zero.
///
/// @details Peephole rewrites often recognise the canonical pattern of moving
///          or comparing against literal zero.  The helper inspects the
///          discriminant and payload of @p operand to confirm it is an
///          @ref OpImm value whose @ref OpImm::val equals zero.  Using a helper
///          keeps the pattern checks readable at the call site.
///
/// @param operand Operand drawn from a Machine IR instruction.
/// @return @c true when the operand is an immediate zero literal.
[[nodiscard]] bool isZeroImm(const Operand &operand) noexcept
{
    const auto *imm = std::get_if<OpImm>(&operand);
    return imm != nullptr && imm->val == 0;
}

/// @brief Check whether an operand refers to a general-purpose register.
///
/// @details The peepholes implemented here only fold when the destination is a
///          GPR because the rewrite replaces `mov`/`cmp` with register-to-self
///          forms.  This helper verifies that @p operand holds an @ref OpReg and
///          that the register class is @ref RegClass::GPR.
///
/// @param operand Operand to classify.
/// @return @c true when the operand is a GPR register reference.
[[nodiscard]] bool isGprReg(const Operand &operand) noexcept
{
    const auto *reg = std::get_if<OpReg>(&operand);
    return reg != nullptr && reg->cls == RegClass::GPR;
}

/// @brief Check if an operand is a physical register.
[[nodiscard]] bool isPhysReg(const Operand &operand) noexcept
{
    const auto *reg = std::get_if<OpReg>(&operand);
    return reg != nullptr && reg->isPhys;
}

/// @brief Check if two register operands refer to the same physical register.
[[nodiscard]] bool samePhysReg(const Operand &a, const Operand &b) noexcept
{
    const auto *regA = std::get_if<OpReg>(&a);
    const auto *regB = std::get_if<OpReg>(&b);
    if (!regA || !regB)
        return false;
    if (!regA->isPhys || !regB->isPhys)
        return false;
    return regA->cls == regB->cls && regA->idOrPhys == regB->idOrPhys;
}

/// @brief Check if an instruction is an identity move (mov r, r).
[[nodiscard]] bool isIdentityMovRR(const MInstr &instr) noexcept
{
    if (instr.opcode != MOpcode::MOVrr)
        return false;
    if (instr.operands.size() != 2)
        return false;
    return samePhysReg(instr.operands[0], instr.operands[1]);
}

/// @brief Check if an instruction is an identity FPR move (movsd d, d).
[[nodiscard]] bool isIdentityMovSDRR(const MInstr &instr) noexcept
{
    if (instr.opcode != MOpcode::MOVSDrr)
        return false;
    if (instr.operands.size() != 2)
        return false;
    return samePhysReg(instr.operands[0], instr.operands[1]);
}

/// @brief Get immediate value from an operand if it is an immediate.
[[nodiscard]] std::optional<int64_t> getImmValue(const Operand &operand) noexcept
{
    const auto *imm = std::get_if<OpImm>(&operand);
    if (imm)
        return imm->val;
    return std::nullopt;
}

/// @brief Check if a value is a power of 2 and return the log2, or -1 if not.
[[nodiscard]] int log2IfPowerOf2(int64_t value) noexcept
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

/// @brief Map of registers to their known constant values from MOVri.
using RegConstMap = std::unordered_map<uint16_t, int64_t>;

/// @brief Update register constant tracking based on an instruction.
void updateKnownConsts(const MInstr &instr, RegConstMap &knownConsts)
{
    // MOVri loads a constant into a register
    if (instr.opcode == MOpcode::MOVri && instr.operands.size() == 2)
    {
        const auto *dst = std::get_if<OpReg>(&instr.operands[0]);
        const auto *imm = std::get_if<OpImm>(&instr.operands[1]);
        if (dst && dst->isPhys && imm)
        {
            knownConsts[dst->idOrPhys] = imm->val;
            return;
        }
    }

    // Any instruction that defines a register invalidates the constant
    switch (instr.opcode)
    {
        case MOpcode::MOVrr:
        case MOpcode::MOVmr:
        case MOpcode::CMOVNErr:
        case MOpcode::LEA:
        case MOpcode::ADDrr:
        case MOpcode::ADDri:
        case MOpcode::ANDrr:
        case MOpcode::ANDri:
        case MOpcode::ORrr:
        case MOpcode::ORri:
        case MOpcode::XORrr:
        case MOpcode::XORri:
        case MOpcode::XORrr32:
        case MOpcode::SUBrr:
        case MOpcode::SHLri:
        case MOpcode::SHLrc:
        case MOpcode::SHRri:
        case MOpcode::SHRrc:
        case MOpcode::SARri:
        case MOpcode::SARrc:
        case MOpcode::IMULrr:
        case MOpcode::DIVS64rr:
        case MOpcode::REMS64rr:
        case MOpcode::DIVU64rr:
        case MOpcode::REMU64rr:
        case MOpcode::SETcc:
        case MOpcode::MOVZXrr32:
            if (!instr.operands.empty())
            {
                const auto *dst = std::get_if<OpReg>(&instr.operands[0]);
                if (dst && dst->isPhys)
                    knownConsts.erase(dst->idOrPhys);
            }
            break;

        case MOpcode::CQO:
            // CQO modifies RDX
            knownConsts.erase(static_cast<uint16_t>(PhysReg::RDX));
            break;

        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
            // IDIV/DIV modify RAX and RDX
            knownConsts.erase(static_cast<uint16_t>(PhysReg::RAX));
            knownConsts.erase(static_cast<uint16_t>(PhysReg::RDX));
            break;

        default:
            break;
    }

    // Calls invalidate all caller-saved registers
    if (instr.opcode == MOpcode::CALL)
    {
        // x86-64 caller-saved: RAX, RCX, RDX, RSI, RDI, R8-R11
        knownConsts.erase(static_cast<uint16_t>(PhysReg::RAX));
        knownConsts.erase(static_cast<uint16_t>(PhysReg::RCX));
        knownConsts.erase(static_cast<uint16_t>(PhysReg::RDX));
        knownConsts.erase(static_cast<uint16_t>(PhysReg::RSI));
        knownConsts.erase(static_cast<uint16_t>(PhysReg::RDI));
        knownConsts.erase(static_cast<uint16_t>(PhysReg::R8));
        knownConsts.erase(static_cast<uint16_t>(PhysReg::R9));
        knownConsts.erase(static_cast<uint16_t>(PhysReg::R10));
        knownConsts.erase(static_cast<uint16_t>(PhysReg::R11));
    }
}

/// @brief Get constant value for a register if known.
[[nodiscard]] std::optional<int64_t> getConstValue(const Operand &operand, const RegConstMap &knownConsts)
{
    const auto *reg = std::get_if<OpReg>(&operand);
    if (!reg || !reg->isPhys || reg->cls != RegClass::GPR)
        return std::nullopt;
    auto it = knownConsts.find(reg->idOrPhys);
    if (it != knownConsts.end())
        return it->second;
    return std::nullopt;
}

/// @brief Check if an instruction defines a given physical register.
[[nodiscard]] bool definesReg(const MInstr &instr, const Operand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;

    const auto *targetReg = std::get_if<OpReg>(&reg);
    if (!targetReg)
        return false;

    // Most x86-64 instructions have the destination as the first operand.
    switch (instr.opcode)
    {
        case MOpcode::MOVrr:
        case MOpcode::MOVmr:
        case MOpcode::CMOVNErr:
        case MOpcode::MOVri:
        case MOpcode::LEA:
        case MOpcode::ADDrr:
        case MOpcode::ADDri:
        case MOpcode::ANDrr:
        case MOpcode::ANDri:
        case MOpcode::ORrr:
        case MOpcode::ORri:
        case MOpcode::XORrr:
        case MOpcode::XORri:
        case MOpcode::XORrr32:
        case MOpcode::SUBrr:
        case MOpcode::SHLri:
        case MOpcode::SHLrc:
        case MOpcode::SHRri:
        case MOpcode::SHRrc:
        case MOpcode::SARri:
        case MOpcode::SARrc:
        case MOpcode::IMULrr:
        case MOpcode::DIVS64rr:
        case MOpcode::REMS64rr:
        case MOpcode::DIVU64rr:
        case MOpcode::REMU64rr:
        case MOpcode::SETcc:
        case MOpcode::MOVZXrr32:
        case MOpcode::MOVSDrr:
        case MOpcode::MOVSDmr:
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
        case MOpcode::FDIV:
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
            if (!instr.operands.empty() && samePhysReg(instr.operands[0], reg))
                return true;
            break;

        default:
            break;
    }
    return false;
}

/// @brief Check if an instruction uses a given physical register as a source.
[[nodiscard]] bool usesReg(const MInstr &instr, const Operand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;

    switch (instr.opcode)
    {
        case MOpcode::MOVrr:
        case MOpcode::MOVSDrr:
            // dst, src - check src
            if (instr.operands.size() >= 2 && samePhysReg(instr.operands[1], reg))
                return true;
            break;

        case MOpcode::ADDrr:
        case MOpcode::SUBrr:
        case MOpcode::ANDrr:
        case MOpcode::ORrr:
        case MOpcode::XORrr:
        case MOpcode::IMULrr:
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
        case MOpcode::FDIV:
            // dst, src - dst is both read and written, check both
            if (instr.operands.size() >= 1 && samePhysReg(instr.operands[0], reg))
                return true;
            if (instr.operands.size() >= 2 && samePhysReg(instr.operands[1], reg))
                return true;
            break;

        case MOpcode::CMPrr:
        case MOpcode::TESTrr:
        case MOpcode::UCOMIS:
            // lhs, rhs - check both
            if (instr.operands.size() >= 1 && samePhysReg(instr.operands[0], reg))
                return true;
            if (instr.operands.size() >= 2 && samePhysReg(instr.operands[1], reg))
                return true;
            break;

        case MOpcode::CMPri:
            // reg, imm - check reg
            if (instr.operands.size() >= 1 && samePhysReg(instr.operands[0], reg))
                return true;
            break;

        case MOpcode::MOVrm:
        case MOpcode::MOVSDrm:
            // mem, src - check src (operands[1] is the source register)
            if (instr.operands.size() >= 2 && samePhysReg(instr.operands[1], reg))
                return true;
            break;

        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
            // dst, src - check src
            if (instr.operands.size() >= 2 && samePhysReg(instr.operands[1], reg))
                return true;
            break;

        default:
            break;
    }
    return false;
}

/// @brief Check if a register is an argument-passing register (RDI, RSI, RDX, RCX, R8, R9).
[[nodiscard]] bool isArgReg(const Operand &operand) noexcept
{
    const auto *reg = std::get_if<OpReg>(&operand);
    if (!reg || !reg->isPhys || reg->cls != RegClass::GPR)
        return false;
    const auto pr = static_cast<PhysReg>(reg->idOrPhys);
    return pr == PhysReg::RDI || pr == PhysReg::RSI || pr == PhysReg::RDX ||
           pr == PhysReg::RCX || pr == PhysReg::R8 || pr == PhysReg::R9;
}

/// @brief Check if an instruction is an unconditional jump to a specific label.
[[nodiscard]] bool isJumpTo(const MInstr &instr, const std::string &label) noexcept
{
    if (instr.opcode != MOpcode::JMP)
        return false;
    if (instr.operands.empty())
        return false;
    const auto *lbl = std::get_if<OpLabel>(&instr.operands[0]);
    return lbl && lbl->name == label;
}

/// @brief Rewrite a MOV immediate-to-register into XOR to synthesize zero.
///
/// @details Zeroing a register via `xor reg, reg` encodes shorter and avoids
///          materialising literal zero immediates.  The helper updates
///          @p instr in place by clearing existing operands, switching the
///          opcode to @ref MOpcode::XORrr32, and inserting the supplied register
///          operand twice to match the canonical encoding.
///
/// @param instr Instruction to mutate.
/// @param regOperand Register operand that will be zeroed.
void rewriteToXor(MInstr &instr, Operand regOperand)
{
    instr.opcode = MOpcode::XORrr32;
    instr.operands.clear();
    instr.operands.push_back(regOperand);
    instr.operands.push_back(regOperand);
}

/// @brief Convert a compare-against-zero into a register self-test.
///
/// @details The transformation mirrors the XOR rewrite but targets `cmp`.
///          Testing a register against itself produces the same flag pattern as
///          comparing to zero while avoiding the immediate operand.  The helper
///          swaps in @ref MOpcode::TESTrr and duplicates the register operand so
///          callers can apply the optimisation with a single call.
///
/// @param instr Instruction to rewrite in place.
/// @param regOperand Register operand participating in the test.
void rewriteToTest(MInstr &instr, Operand regOperand)
{
    instr.opcode = MOpcode::TESTrr;
    instr.operands.clear();
    instr.operands.push_back(regOperand);
    instr.operands.push_back(regOperand);
}

/// @brief Rewrite arithmetic identity operations.
///
/// Patterns:
/// - add reg, #0 -> identity (mark for removal)
/// - shl/shr/sar reg, #0 -> identity (mark for removal)
///
/// @param instr Instruction to check.
/// @param stats Statistics to update.
/// @return true if the instruction is an identity and should be removed.
[[nodiscard]] bool tryArithmeticIdentity(const MInstr &instr, PeepholeStats &stats)
{
    switch (instr.opcode)
    {
        case MOpcode::ADDri:
            // add reg, #0 -> no-op
            if (instr.operands.size() == 2 && isZeroImm(instr.operands[1]))
            {
                ++stats.arithmeticIdentities;
                return true;
            }
            break;

        case MOpcode::SHLri:
        case MOpcode::SHRri:
        case MOpcode::SARri:
            // shift by 0 -> no-op
            if (instr.operands.size() == 2 && isZeroImm(instr.operands[1]))
            {
                ++stats.arithmeticIdentities;
                return true;
            }
            break;

        default:
            break;
    }
    return false;
}

/// @brief Apply strength reduction: mul by power-of-2 -> shift left.
///
/// Pattern: imul dst, src where src is a known power-of-2 constant -> shl dst, #log2(src)
///
/// @param instr Instruction to check and potentially rewrite.
/// @param knownConsts Map of registers to known constant values.
/// @param stats Statistics to update.
/// @return true if reduction was applied.
[[nodiscard]] bool tryStrengthReduction(MInstr &instr, const RegConstMap &knownConsts, PeepholeStats &stats)
{
    if (instr.opcode != MOpcode::IMULrr)
        return false;
    if (instr.operands.size() != 2)
        return false;

    // imul dst, src - check if src is a known power-of-2 constant
    auto srcConst = getConstValue(instr.operands[1], knownConsts);
    if (!srcConst)
        return false;

    int shiftAmount = log2IfPowerOf2(*srcConst);
    if (shiftAmount < 0 || shiftAmount > 63)
        return false;

    // Rewrite: imul dst, src -> shl dst, #shift
    instr.opcode = MOpcode::SHLri;
    instr.operands[1] = OpImm{shiftAmount};
    ++stats.strengthReductions;
    return true;
}

/// @brief Try to fold consecutive moves: mov r1, r2; mov r3, r1 -> mov r3, r2
///
/// This optimization is only safe when r1 is not used after the second move
/// within the same basic block, and when there are no intervening calls that
/// might implicitly use argument registers.
///
/// @param instrs Vector of instructions in a basic block.
/// @param idx Index of the first instruction to check.
/// @param stats Statistics to update.
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
    const bool firstIsMovRR = (first.opcode == MOpcode::MOVrr && first.operands.size() == 2);
    const bool secondIsMovRR = (second.opcode == MOpcode::MOVrr && second.operands.size() == 2);

    if (!firstIsMovRR || !secondIsMovRR)
        return false;

    // first: dst=r1, src=r2
    // second: dst=r3, src=r1
    // Check if second.src == first.dst
    if (!samePhysReg(second.operands[1], first.operands[0]))
        return false;

    // Don't fold if the intermediate register is an argument register and
    // there's a call instruction nearby.
    const Operand &r1 = first.operands[0];
    if (isArgReg(r1))
    {
        // Check for call instructions after the second move
        for (std::size_t i = idx + 2; i < instrs.size(); ++i)
        {
            if (instrs[i].opcode == MOpcode::CALL)
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
    const Operand originalSrc = first.operands[1]; // Save the original source
    second.operands[1] = originalSrc;              // second.src = first.src
    first.operands[0] = first.operands[1];         // Make first identity (mov r2, r2)
    ++stats.consecutiveMovsFolded;
    return true;
}

// Forward declaration
void removeMarkedInstructions(std::vector<MInstr> &instrs, const std::vector<bool> &toRemove);

/// @brief Check if an instruction modifies RSP (the stack pointer).
/// @details RSP modifications always have implicit side effects because they
///          change the stack frame layout. These must never be eliminated.
[[nodiscard]] bool modifiesRSP(const MInstr &instr) noexcept
{
    if (instr.operands.empty())
        return false;

    // Check if the first operand (destination) is RSP
    const auto *reg = std::get_if<OpReg>(&instr.operands[0]);
    if (!reg || !reg->isPhys)
        return false;

    return static_cast<PhysReg>(reg->idOrPhys) == PhysReg::RSP;
}

/// @brief Check if an instruction has observable side effects.
/// @details Instructions with side effects cannot be eliminated even if their
///          result is unused. This includes memory stores, calls, control flow,
///          and RSP modifications (which affect the stack frame).
[[nodiscard]] bool hasSideEffects(const MInstr &instr) noexcept
{
    // RSP modifications are always significant - they affect the stack frame
    if (modifiesRSP(instr))
        return true;

    switch (instr.opcode)
    {
        // Memory stores
        case MOpcode::MOVrm:
        case MOpcode::MOVSDrm:
        // Control flow
        case MOpcode::JMP:
        case MOpcode::JCC:
        case MOpcode::CALL:
        case MOpcode::RET:
        case MOpcode::UD2:
        case MOpcode::LABEL:
        // Instructions that set flags used by subsequent JCC
        case MOpcode::CMPrr:
        case MOpcode::CMPri:
        case MOpcode::TESTrr:
        case MOpcode::UCOMIS:
        // Division instructions (can trap)
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
        case MOpcode::CQO:
        // Parallel copy pseudo (used in phi lowering)
        case MOpcode::PX_COPY:
            return true;
        default:
            return false;
    }
}

/// @brief Get the destination register from an instruction, if it defines one.
[[nodiscard]] std::optional<uint16_t> getDefReg(const MInstr &instr) noexcept
{
    if (instr.operands.empty())
        return std::nullopt;

    switch (instr.opcode)
    {
        case MOpcode::MOVrr:
        case MOpcode::MOVmr:
        case MOpcode::MOVri:
        case MOpcode::CMOVNErr:
        case MOpcode::LEA:
        case MOpcode::ADDrr:
        case MOpcode::ADDri:
        case MOpcode::ANDrr:
        case MOpcode::ANDri:
        case MOpcode::ORrr:
        case MOpcode::ORri:
        case MOpcode::XORrr:
        case MOpcode::XORri:
        case MOpcode::XORrr32:
        case MOpcode::SUBrr:
        case MOpcode::SHLri:
        case MOpcode::SHLrc:
        case MOpcode::SHRri:
        case MOpcode::SHRrc:
        case MOpcode::SARri:
        case MOpcode::SARrc:
        case MOpcode::IMULrr:
        case MOpcode::DIVS64rr:
        case MOpcode::REMS64rr:
        case MOpcode::DIVU64rr:
        case MOpcode::REMU64rr:
        case MOpcode::SETcc:
        case MOpcode::MOVZXrr32:
        case MOpcode::MOVSDrr:
        case MOpcode::MOVSDmr:
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
        case MOpcode::FDIV:
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
        {
            const auto *reg = std::get_if<OpReg>(&instr.operands[0]);
            if (reg && reg->isPhys)
                return reg->idOrPhys;
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

/// @brief Collect all physical registers used by an instruction.
void collectUsedRegs(const MInstr &instr, std::unordered_set<uint16_t> &usedRegs)
{
    // Helper to add a register if it's physical
    auto addIfPhysReg = [&usedRegs](const Operand &op)
    {
        const auto *reg = std::get_if<OpReg>(&op);
        if (reg && reg->isPhys)
            usedRegs.insert(reg->idOrPhys);
    };

    // Helper to add registers from memory operand
    auto addMemRegs = [&usedRegs](const Operand &op)
    {
        const auto *mem = std::get_if<OpMem>(&op);
        if (mem)
        {
            // Base register is always valid in OpMem
            if (mem->base.isPhys)
                usedRegs.insert(mem->base.idOrPhys);
            // Index register is only valid when hasIndex is true
            if (mem->hasIndex && mem->index.isPhys)
                usedRegs.insert(mem->index.idOrPhys);
        }
    };

    switch (instr.opcode)
    {
        case MOpcode::MOVrr:
        case MOpcode::MOVSDrr:
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
            // dst, src - use src
            if (instr.operands.size() >= 2)
                addIfPhysReg(instr.operands[1]);
            break;

        case MOpcode::ADDrr:
        case MOpcode::SUBrr:
        case MOpcode::ANDrr:
        case MOpcode::ORrr:
        case MOpcode::XORrr:
        case MOpcode::IMULrr:
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
        case MOpcode::FDIV:
            // dst, src - both are used (dst is read-modify-write)
            if (instr.operands.size() >= 1)
                addIfPhysReg(instr.operands[0]);
            if (instr.operands.size() >= 2)
                addIfPhysReg(instr.operands[1]);
            break;

        case MOpcode::MOVmr:
        case MOpcode::MOVSDmr:
            // dst, mem - use mem's registers
            if (instr.operands.size() >= 2)
                addMemRegs(instr.operands[1]);
            break;

        case MOpcode::MOVrm:
        case MOpcode::MOVSDrm:
            // mem, src - use both mem's registers and src
            if (instr.operands.size() >= 1)
                addMemRegs(instr.operands[0]);
            if (instr.operands.size() >= 2)
                addIfPhysReg(instr.operands[1]);
            break;

        case MOpcode::CMPrr:
        case MOpcode::TESTrr:
        case MOpcode::UCOMIS:
            // lhs, rhs - use both
            if (instr.operands.size() >= 1)
                addIfPhysReg(instr.operands[0]);
            if (instr.operands.size() >= 2)
                addIfPhysReg(instr.operands[1]);
            break;

        case MOpcode::CMPri:
        case MOpcode::ADDri:
        case MOpcode::ANDri:
        case MOpcode::ORri:
        case MOpcode::XORri:
        case MOpcode::SHLri:
        case MOpcode::SHRri:
        case MOpcode::SARri:
            // dst, imm - dst is read-modify-write
            if (instr.operands.size() >= 1)
                addIfPhysReg(instr.operands[0]);
            break;

        case MOpcode::LEA:
            // dst, mem - use mem's registers
            if (instr.operands.size() >= 2)
                addMemRegs(instr.operands[1]);
            break;

        case MOpcode::CALL:
            // Calls may use all argument registers
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RDI));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RSI));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RDX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RCX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::R8));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::R9));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
            // XMM0-7 for float args
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM0));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM1));
            break;

        case MOpcode::RET:
            // Return uses RAX (or XMM0 for floats)
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM0));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
            break;

        default:
            // For other instructions, conservatively assume all operand registers are used
            for (const auto &op : instr.operands)
            {
                addIfPhysReg(op);
                addMemRegs(op);
            }
            break;
    }
}

/// @brief Run dead code elimination on a basic block.
/// @details Performs backward liveness analysis and removes instructions that
///          define registers which are never used. Iterates until fixpoint.
/// @param instrs Instructions in the basic block.
/// @param stats Statistics counter to update.
/// @return Number of instructions eliminated.
std::size_t runBlockDCE(std::vector<MInstr> &instrs, PeepholeStats &stats)
{
    if (instrs.empty())
        return 0;

    std::size_t totalEliminated = 0;
    bool changed = true;

    while (changed)
    {
        changed = false;

        // Build liveness: for each instruction, which registers are live after it?
        // Work backwards from the end of the block
        std::unordered_set<uint16_t> liveRegs;

        // At block exit, assume all callee-saved and return registers are live
        // On Windows x64, callee-saved GPRs are: RBX, RBP, RDI, RSI, RSP, R12-R15
        liveRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::RBX));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::RBP));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::RDI));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::RSI));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::R12));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::R13));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::R14));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::R15));
        liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM0));

        std::vector<bool> toRemove(instrs.size(), false);

        // Process instructions in reverse order
        for (std::size_t i = instrs.size(); i > 0; --i)
        {
            const std::size_t idx = i - 1;
            const auto &instr = instrs[idx];

            // Instructions with side effects cannot be eliminated
            if (hasSideEffects(instr))
            {
                // Labels are jump targets - any register could be live at entry
                // because control can flow there from a branch elsewhere.
                // Mark all allocatable registers as live to prevent incorrectly
                // eliminating code that precedes a branch to this label.
                if (instr.opcode == MOpcode::LABEL)
                {
                    // GPRs
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::RBX));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::RCX));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::RDX));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::RSI));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::RDI));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::R8));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::R9));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::R10));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::R11));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::R12));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::R13));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::R14));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::R15));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::RBP));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
                    // XMM registers
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM0));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM1));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM2));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM3));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM4));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM5));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM6));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM7));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM8));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM9));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM10));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM11));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM12));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM13));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM14));
                    liveRegs.insert(static_cast<uint16_t>(PhysReg::XMM15));
                }
                collectUsedRegs(instr, liveRegs);
                continue;
            }

            // Check if the instruction defines a register
            auto defReg = getDefReg(instr);
            if (!defReg.has_value())
            {
                // No def, keep and collect uses
                collectUsedRegs(instr, liveRegs);
                continue;
            }

            // If the defined register is not live, this instruction is dead
            if (liveRegs.find(*defReg) == liveRegs.end())
            {
                toRemove[idx] = true;
                ++stats.deadCodeEliminated;
                changed = true;
                continue;
            }

            // Remove def from live set, add uses
            liveRegs.erase(*defReg);
            collectUsedRegs(instr, liveRegs);
        }

        // Remove dead instructions
        if (changed)
        {
            removeMarkedInstructions(instrs, toRemove);
            totalEliminated += std::count(toRemove.begin(), toRemove.end(), true);
        }
    }

    return totalEliminated;
}

/// @brief Remove instructions marked for deletion from a basic block.
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

/// @brief Apply local peephole simplifications to the provided Machine IR.
///
/// @details Walks each instruction in every block and matches simple patterns
///          that can be reduced without altering control flow: moving a zero
///          immediate into a GPR and comparing a GPR against zero.  When a match
///          is found the instruction is rewritten in place using the helpers
///          above.  The routine intentionally avoids allocating additional data
///          structures so it can be invoked late in the pipeline without
///          impacting compile time.
///
/// @param fn Machine function to optimise.
void runPeepholes(MFunction &fn)
{
    PeepholeStats stats;

    for (auto &block : fn.blocks)
    {
        auto &instrs = block.instructions;
        if (instrs.empty())
            continue;

        // Pass 1: Build register constant map and apply rewrites
        RegConstMap knownConsts;
        std::vector<bool> toRemove(instrs.size(), false);

        for (std::size_t i = 0; i < instrs.size(); ++i)
        {
            auto &instr = instrs[i];

            // Track constants loaded via MOVri
            updateKnownConsts(instr, knownConsts);

            switch (instr.opcode)
            {
                case MOpcode::MOVri:
                {
                    if (instr.operands.size() != 2)
                        break;

                    if (!isGprReg(instr.operands[0]) || !isZeroImm(instr.operands[1]))
                        break;

                    rewriteToXor(instr, instr.operands[0]);
                    ++stats.movZeroToXor;
                    break;
                }
                case MOpcode::CMPri:
                {
                    if (instr.operands.size() != 2)
                        break;

                    if (!isGprReg(instr.operands[0]) || !isZeroImm(instr.operands[1]))
                        break;

                    rewriteToTest(instr, instr.operands[0]);
                    ++stats.cmpZeroToTest;
                    break;
                }
                default:
                    break;
            }

            // Try arithmetic identity elimination (add #0, shift #0)
            if (tryArithmeticIdentity(instr, stats))
            {
                toRemove[i] = true;
                continue;
            }

            // Try strength reduction (mul power-of-2 -> shift)
            (void)tryStrengthReduction(instr, knownConsts, stats);
        }

        // Pass 2: Try to fold consecutive moves
        for (std::size_t i = 0; i + 1 < instrs.size(); ++i)
        {
            (void)tryFoldConsecutiveMoves(instrs, i, stats);
        }

        // Pass 3: Mark identity moves for removal
        for (std::size_t i = 0; i < instrs.size(); ++i)
        {
            if (isIdentityMovRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityMovesRemoved;
            }
            else if (isIdentityMovSDRR(instrs[i]))
            {
                toRemove[i] = true;
                ++stats.identityMovesRemoved;
            }
        }

        // Pass 4: Remove marked instructions
        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool v) { return v; }))
        {
            removeMarkedInstructions(instrs, toRemove);
        }

        // Pass 5: Dead code elimination
        runBlockDCE(instrs, stats);
    }

    // Pass 6: Block layout optimization - move cold blocks to the end
    // Cold blocks are those containing UD2 (trap) or with labels suggesting error handling
    if (fn.blocks.size() > 2)
    {
        std::vector<std::size_t> coldIndices;
        std::vector<std::size_t> hotIndices;

        // First block (entry) is always hot and stays first
        hotIndices.push_back(0);

        // Classify remaining blocks
        for (std::size_t bi = 1; bi < fn.blocks.size(); ++bi)
        {
            const auto &block = fn.blocks[bi];
            bool isCold = false;

            // Check for trap/error indicators in label
            const auto &label = block.label;
            if (label.find("trap") != std::string::npos ||
                label.find("error") != std::string::npos ||
                label.find("panic") != std::string::npos ||
                label.find("unreachable") != std::string::npos)
            {
                isCold = true;
            }

            // Check for UD2 instruction (trap)
            if (!isCold)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.opcode == MOpcode::UD2)
                    {
                        isCold = true;
                        break;
                    }
                }
            }

            if (isCold)
                coldIndices.push_back(bi);
            else
                hotIndices.push_back(bi);
        }

        // Only reorder if we found cold blocks
        if (!coldIndices.empty() && hotIndices.size() > 1)
        {
            std::vector<MBasicBlock> newBlocks;
            newBlocks.reserve(fn.blocks.size());

            // Add hot blocks first
            for (std::size_t idx : hotIndices)
                newBlocks.push_back(std::move(fn.blocks[idx]));

            // Add cold blocks at the end
            for (std::size_t idx : coldIndices)
            {
                newBlocks.push_back(std::move(fn.blocks[idx]));
                ++stats.coldBlocksMoved;
            }

            fn.blocks = std::move(newBlocks);
        }
    }

    // Pass 7: Remove jumps to the immediately following block
    for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi)
    {
        auto &block = fn.blocks[bi];
        const auto &nextBlock = fn.blocks[bi + 1];

        if (block.instructions.empty())
            continue;

        // Check if the last instruction is an unconditional jump to the next block
        auto &lastInstr = block.instructions.back();
        if (isJumpTo(lastInstr, nextBlock.label))
        {
            block.instructions.pop_back();
            ++stats.branchesToNextRemoved;
        }
    }
}

} // namespace viper::codegen::x64
