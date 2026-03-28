//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/PeepholeCommon.hpp
// Purpose: Shared utility functions and types for x86-64 peephole sub-passes.
//
// Key invariants:
//   - All helpers are pure queries or in-place rewrites; they never allocate
//     persistent state.
//   - Register classification must stay in sync with MachineIR opcode additions.
//
// Ownership/Lifetime:
//   - Header-only; no dynamic state.
//
// Links: src/codegen/x86_64/Peephole.hpp, docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "codegen/common/PeepholeUtil.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace viper::codegen::x64::peephole {

// Bring the shared compaction helper into this namespace scope.
using viper::codegen::common::removeMarkedInstructions;

// ---- Statistics -------------------------------------------------------------

/// @brief Statistics tracking for peephole optimizations.
struct PeepholeStats {
    std::size_t movZeroToXor{0};
    std::size_t cmpZeroToTest{0};
    std::size_t arithmeticIdentities{0};
    std::size_t strengthReductions{0};
    std::size_t identityMovesRemoved{0};
    std::size_t consecutiveMovsFolded{0};
    std::size_t branchesToNextRemoved{0};
    std::size_t deadCodeEliminated{0};
    std::size_t coldBlocksMoved{0};
    std::size_t branchesInverted{0};
    std::size_t blocksReordered{0};
    std::size_t branchChainsEliminated{0};

    [[nodiscard]] std::size_t total() const noexcept {
        return movZeroToXor + cmpZeroToTest + arithmeticIdentities + strengthReductions +
               identityMovesRemoved + consecutiveMovsFolded + branchesToNextRemoved +
               deadCodeEliminated + coldBlocksMoved + branchesInverted + blocksReordered +
               branchChainsEliminated;
    }
};

// ---- Pre-computed register sets ---------------------------------------------

/// @brief Registers assumed live at block exit (callee-saved + return registers).
/// @details Pre-computed to avoid 11 individual insert() calls per DCE iteration.
inline const std::vector<uint16_t> &getBlockExitLiveRegs() {
    static const std::vector<uint16_t> regs = {
        static_cast<uint16_t>(PhysReg::RAX),
        static_cast<uint16_t>(PhysReg::RBX),
        static_cast<uint16_t>(PhysReg::RBP),
        static_cast<uint16_t>(PhysReg::RDI),
        static_cast<uint16_t>(PhysReg::RSI),
        static_cast<uint16_t>(PhysReg::RSP),
        static_cast<uint16_t>(PhysReg::R12),
        static_cast<uint16_t>(PhysReg::R13),
        static_cast<uint16_t>(PhysReg::R14),
        static_cast<uint16_t>(PhysReg::R15),
        static_cast<uint16_t>(PhysReg::XMM0),
    };
    return regs;
}

/// @brief All allocatable registers (GPR + XMM), marked live at labels.
/// @details Pre-computed to avoid 32 individual insert() calls when hitting a label.
inline const std::vector<uint16_t> &getAllAllocatableRegs() {
    static const std::vector<uint16_t> regs = {
        // GPRs
        static_cast<uint16_t>(PhysReg::RAX),
        static_cast<uint16_t>(PhysReg::RBX),
        static_cast<uint16_t>(PhysReg::RCX),
        static_cast<uint16_t>(PhysReg::RDX),
        static_cast<uint16_t>(PhysReg::RSI),
        static_cast<uint16_t>(PhysReg::RDI),
        static_cast<uint16_t>(PhysReg::R8),
        static_cast<uint16_t>(PhysReg::R9),
        static_cast<uint16_t>(PhysReg::R10),
        static_cast<uint16_t>(PhysReg::R11),
        static_cast<uint16_t>(PhysReg::R12),
        static_cast<uint16_t>(PhysReg::R13),
        static_cast<uint16_t>(PhysReg::R14),
        static_cast<uint16_t>(PhysReg::R15),
        static_cast<uint16_t>(PhysReg::RBP),
        static_cast<uint16_t>(PhysReg::RSP),
        // XMM registers
        static_cast<uint16_t>(PhysReg::XMM0),
        static_cast<uint16_t>(PhysReg::XMM1),
        static_cast<uint16_t>(PhysReg::XMM2),
        static_cast<uint16_t>(PhysReg::XMM3),
        static_cast<uint16_t>(PhysReg::XMM4),
        static_cast<uint16_t>(PhysReg::XMM5),
        static_cast<uint16_t>(PhysReg::XMM6),
        static_cast<uint16_t>(PhysReg::XMM7),
        static_cast<uint16_t>(PhysReg::XMM8),
        static_cast<uint16_t>(PhysReg::XMM9),
        static_cast<uint16_t>(PhysReg::XMM10),
        static_cast<uint16_t>(PhysReg::XMM11),
        static_cast<uint16_t>(PhysReg::XMM12),
        static_cast<uint16_t>(PhysReg::XMM13),
        static_cast<uint16_t>(PhysReg::XMM14),
        static_cast<uint16_t>(PhysReg::XMM15),
    };
    return regs;
}

// ---- Operand query helpers --------------------------------------------------

/// @brief Test whether an operand is the immediate integer zero.
[[nodiscard]] inline bool isZeroImm(const Operand &operand) noexcept {
    const auto *imm = std::get_if<OpImm>(&operand);
    return imm != nullptr && imm->val == 0;
}

/// @brief Check whether an operand refers to a general-purpose register.
[[nodiscard]] inline bool isGprReg(const Operand &operand) noexcept {
    const auto *reg = std::get_if<OpReg>(&operand);
    return reg != nullptr && reg->cls == RegClass::GPR;
}

/// @brief Check if an operand is a physical register.
[[nodiscard]] inline bool isPhysReg(const Operand &operand) noexcept {
    const auto *reg = std::get_if<OpReg>(&operand);
    return reg != nullptr && reg->isPhys;
}

/// @brief Check if two register operands refer to the same physical register.
[[nodiscard]] inline bool samePhysReg(const Operand &a, const Operand &b) noexcept {
    const auto *regA = std::get_if<OpReg>(&a);
    const auto *regB = std::get_if<OpReg>(&b);
    if (!regA || !regB)
        return false;
    if (!regA->isPhys || !regB->isPhys)
        return false;
    return regA->cls == regB->cls && regA->idOrPhys == regB->idOrPhys;
}

/// @brief Check if an instruction is an identity move (mov r, r).
[[nodiscard]] inline bool isIdentityMovRR(const MInstr &instr) noexcept {
    if (instr.opcode != MOpcode::MOVrr)
        return false;
    if (instr.operands.size() != 2)
        return false;
    return samePhysReg(instr.operands[0], instr.operands[1]);
}

/// @brief Check if an instruction is an identity FPR move (movsd d, d).
[[nodiscard]] inline bool isIdentityMovSDRR(const MInstr &instr) noexcept {
    if (instr.opcode != MOpcode::MOVSDrr)
        return false;
    if (instr.operands.size() != 2)
        return false;
    return samePhysReg(instr.operands[0], instr.operands[1]);
}

/// @brief Get immediate value from an operand if it is an immediate.
[[nodiscard]] inline std::optional<int64_t> getImmValue(const Operand &operand) noexcept {
    const auto *imm = std::get_if<OpImm>(&operand);
    if (imm)
        return imm->val;
    return std::nullopt;
}

/// @brief Check if a value is a power of 2 and return the log2, or -1 if not.
[[nodiscard]] inline int log2IfPowerOf2(int64_t value) noexcept {
    if (value <= 0)
        return -1;
    if ((value & (value - 1)) != 0)
        return -1; // not a power of 2
    int log = 0;
    while ((1LL << log) < value)
        ++log;
    return log;
}

// ---- Constant tracking ------------------------------------------------------

/// @brief Array of known constant values indexed by physical register ID.
///
/// Each entry holds the constant loaded into that physical register by a
/// recent MOVri, or nullopt if the register's value is unknown.  Indexed
/// directly by static_cast<uint16_t>(PhysReg); the x86-64 PhysReg enum
/// has 32 entries (RAX=0 ... XMM15=31) so the array is always in-bounds.
using RegConstMap = std::array<std::optional<int64_t>, 32>;

/// @brief Update register constant tracking based on an instruction.
inline void updateKnownConsts(const MInstr &instr, RegConstMap &knownConsts) {
    // MOVri loads a constant into a register
    if (instr.opcode == MOpcode::MOVri && instr.operands.size() == 2) {
        const auto *dst = std::get_if<OpReg>(&instr.operands[0]);
        const auto *imm = std::get_if<OpImm>(&instr.operands[1]);
        if (dst && dst->isPhys && dst->idOrPhys < 32 && imm) {
            knownConsts[dst->idOrPhys] = imm->val;
            return;
        }
    }

    // Any instruction that defines a register invalidates the constant
    switch (instr.opcode) {
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
            if (!instr.operands.empty()) {
                const auto *dst = std::get_if<OpReg>(&instr.operands[0]);
                if (dst && dst->isPhys && dst->idOrPhys < 32)
                    knownConsts[dst->idOrPhys].reset();
            }
            break;

        case MOpcode::CQO:
            // CQO modifies RDX
            knownConsts[static_cast<uint16_t>(PhysReg::RDX)].reset();
            break;

        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
            // IDIV/DIV modify RAX and RDX
            knownConsts[static_cast<uint16_t>(PhysReg::RAX)].reset();
            knownConsts[static_cast<uint16_t>(PhysReg::RDX)].reset();
            break;

        default:
            break;
    }

    // Calls invalidate all caller-saved registers
    if (instr.opcode == MOpcode::CALL) {
        // x86-64 caller-saved: RAX, RCX, RDX, RSI, RDI, R8-R11
        knownConsts[static_cast<uint16_t>(PhysReg::RAX)].reset();
        knownConsts[static_cast<uint16_t>(PhysReg::RCX)].reset();
        knownConsts[static_cast<uint16_t>(PhysReg::RDX)].reset();
        knownConsts[static_cast<uint16_t>(PhysReg::RSI)].reset();
        knownConsts[static_cast<uint16_t>(PhysReg::RDI)].reset();
        knownConsts[static_cast<uint16_t>(PhysReg::R8)].reset();
        knownConsts[static_cast<uint16_t>(PhysReg::R9)].reset();
        knownConsts[static_cast<uint16_t>(PhysReg::R10)].reset();
        knownConsts[static_cast<uint16_t>(PhysReg::R11)].reset();
    }
}

/// @brief Get constant value for a register if known.
[[nodiscard]] inline std::optional<int64_t> getConstValue(const Operand &operand,
                                                          const RegConstMap &knownConsts) {
    const auto *reg = std::get_if<OpReg>(&operand);
    if (!reg || !reg->isPhys || reg->cls != RegClass::GPR || reg->idOrPhys >= 32)
        return std::nullopt;
    return knownConsts[reg->idOrPhys];
}

// ---- Register def/use classification ----------------------------------------

/// @brief Check if an instruction defines a given physical register.
[[nodiscard]] inline bool definesReg(const MInstr &instr, const Operand &reg) noexcept {
    if (!isPhysReg(reg))
        return false;

    // Most x86-64 instructions have the destination as the first operand.
    switch (instr.opcode) {
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
        case MOpcode::MOVUPSmr:
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
        case MOpcode::FDIV:
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
        case MOpcode::MOVQrx:
            if (!instr.operands.empty() && samePhysReg(instr.operands[0], reg))
                return true;
            break;

        default:
            break;
    }
    return false;
}

/// @brief Check if an instruction uses a given physical register as a source.
[[nodiscard]] inline bool usesReg(const MInstr &instr, const Operand &reg) noexcept {
    if (!isPhysReg(reg))
        return false;

    switch (instr.opcode) {
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
        case MOpcode::MOVUPSrm:
            // mem, src - check src (operands[1] is the source register)
            if (instr.operands.size() >= 2 && samePhysReg(instr.operands[1], reg))
                return true;
            break;

        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
        case MOpcode::MOVQrx:
            // dst, src - check src
            if (instr.operands.size() >= 2 && samePhysReg(instr.operands[1], reg))
                return true;
            break;

        default:
            break;
    }
    return false;
}

// ---- Misc helpers -----------------------------------------------------------

/// @brief Check if a register is an argument-passing register (RDI, RSI, RDX, RCX, R8, R9).
[[nodiscard]] inline bool isArgReg(const Operand &operand) noexcept {
    const auto *reg = std::get_if<OpReg>(&operand);
    if (!reg || !reg->isPhys || reg->cls != RegClass::GPR)
        return false;
    const auto pr = static_cast<PhysReg>(reg->idOrPhys);
    return pr == PhysReg::RDI || pr == PhysReg::RSI || pr == PhysReg::RDX || pr == PhysReg::RCX ||
           pr == PhysReg::R8 || pr == PhysReg::R9;
}

/// @brief Check if an instruction is an unconditional jump to a specific label.
[[nodiscard]] inline bool isJumpTo(const MInstr &instr, const std::string &label) noexcept {
    if (instr.opcode != MOpcode::JMP)
        return false;
    if (instr.operands.empty())
        return false;
    const auto *lbl = std::get_if<OpLabel>(&instr.operands[0]);
    return lbl && lbl->name == label;
}

/// @brief Check if any following instruction in this block reads flags.
/// @details IMUL and SHL set flags differently (IMUL sets CF/OF for overflow,
///          SHL sets CF to last shifted bit and OF based on sign change).
///          Transforming IMUL to SHL is only safe when no subsequent instruction
///          reads the flags before they are overwritten by another flag-setting
///          instruction or a block boundary.
/// @param instrs Instruction vector for the basic block.
/// @param idx Index of the instruction being considered for rewrite.
/// @return true if a subsequent instruction reads flags before they are overwritten.
[[nodiscard]] inline bool nextInstrReadsFlags(const std::vector<MInstr> &instrs,
                                              std::size_t idx) noexcept {
    for (std::size_t j = idx + 1; j < instrs.size(); ++j) {
        const auto opc = instrs[j].opcode;
        // Instructions that read flags
        if (opc == MOpcode::JCC || opc == MOpcode::SETcc || opc == MOpcode::CMOVNErr)
            return true;
        // Instructions that overwrite flags — safe to stop scanning
        if (opc == MOpcode::CMPrr || opc == MOpcode::CMPri || opc == MOpcode::TESTrr ||
            opc == MOpcode::ADDrr || opc == MOpcode::ADDri || opc == MOpcode::SUBrr ||
            opc == MOpcode::ANDrr || opc == MOpcode::ANDri || opc == MOpcode::ORrr ||
            opc == MOpcode::ORri || opc == MOpcode::XORrr || opc == MOpcode::XORri ||
            opc == MOpcode::XORrr32 || opc == MOpcode::IMULrr)
            return false;
        // LABEL is a potential branch target — conservatively assume flags are read
        if (opc == MOpcode::LABEL)
            return true;
    }
    return false;
}

} // namespace viper::codegen::x64::peephole
