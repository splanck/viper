//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/DCE.cpp
// Purpose: Dead code elimination peephole sub-pass for the x86-64 backend.
//          Defines x86-64 specific traits for the shared DCE template in
//          PeepholeDCE.hpp.
//
// Key invariants:
//   - RSP modifications are never eliminated (stack frame changes).
//   - Iterates to a fixed point within each basic block.
//   - Division instructions (IDIV/DIV) are treated as side-effecting.
//
// Ownership/Lifetime:
//   - Stateless; delegates to the shared DCE template.
//
// Links: codegen/common/PeepholeDCE.hpp
//
//===----------------------------------------------------------------------===//

#include "DCE.hpp"

#include "codegen/common/PeepholeDCE.hpp"

#include <optional>
#include <unordered_set>

namespace viper::codegen::x64::peephole {
namespace {

/// @brief Check if an instruction modifies RSP (the stack pointer).
[[nodiscard]] bool modifiesRSP(const MInstr &instr) noexcept {
    if (instr.operands.empty())
        return false;

    // Check if the first operand (destination) is RSP
    const auto *reg = std::get_if<OpReg>(&instr.operands[0]);
    if (!reg || !reg->isPhys)
        return false;

    return static_cast<PhysReg>(reg->idOrPhys) == PhysReg::RSP;
}

/// @brief Check if an instruction has observable side effects.
[[nodiscard]] bool dceHasSideEffects(const MInstr &instr) noexcept {
    // RSP modifications are always significant - they affect the stack frame
    if (modifiesRSP(instr))
        return true;

    switch (instr.opcode) {
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
        // Overflow-checked arithmetic pseudos (expand to include JCC)
        case MOpcode::ADDOvfrr:
        case MOpcode::SUBOvfrr:
        case MOpcode::IMULOvfrr:
        // Parallel copy pseudo (used in phi lowering)
        case MOpcode::PX_COPY:
            return true;
        default:
            return false;
    }
}

/// @brief Get the destination register from an instruction, if it defines one.
[[nodiscard]] std::optional<uint16_t> getDefReg(const MInstr &instr) noexcept {
    if (instr.operands.empty())
        return std::nullopt;

    switch (instr.opcode) {
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
        case MOpcode::MOVQxr: {
            const auto *reg = std::get_if<OpReg>(&instr.operands[0]);
            if (reg && reg->isPhys)
                return reg->idOrPhys;
            return std::nullopt;
        }
        // SETcc has operands: (condCode:Imm, dest:RegOrMem).
        // The destination is operand 1, not operand 0.
        case MOpcode::SETcc: {
            if (instr.operands.size() >= 2) {
                const auto *reg = std::get_if<OpReg>(&instr.operands[1]);
                if (reg && reg->isPhys)
                    return reg->idOrPhys;
            }
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

/// @brief Collect all physical registers used by an instruction.
void collectUsedRegs(const MInstr &instr, std::unordered_set<uint16_t> &usedRegs) {
    // Helper to add a register if it's physical
    auto addIfPhysReg = [&usedRegs](const Operand &op) {
        const auto *reg = std::get_if<OpReg>(&op);
        if (reg && reg->isPhys)
            usedRegs.insert(reg->idOrPhys);
    };

    // Helper to add registers from memory operand
    auto addMemRegs = [&usedRegs](const Operand &op) {
        const auto *mem = std::get_if<OpMem>(&op);
        if (mem) {
            // Base register is always valid in OpMem
            if (mem->base.isPhys)
                usedRegs.insert(mem->base.idOrPhys);
            // Index register is only valid when hasIndex is true
            if (mem->hasIndex && mem->index.isPhys)
                usedRegs.insert(mem->index.idOrPhys);
        }
    };

    switch (instr.opcode) {
        case MOpcode::MOVrr:
        case MOpcode::MOVSDrr:
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
        case MOpcode::MOVQrx:
        case MOpcode::MOVQxr:
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
        case MOpcode::MOVUPSmr:
            // dst, mem - use mem's registers
            if (instr.operands.size() >= 2)
                addMemRegs(instr.operands[1]);
            break;

        case MOpcode::MOVrm:
        case MOpcode::MOVSDrm:
        case MOpcode::MOVUPSrm:
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

        case MOpcode::SETcc:
            // SETcc: (condCode:Imm, dest:Reg) — no register uses.
            // The condition code is an immediate and the destination is def-only.
            // Flags (read implicitly) are not tracked as registers.
            break;

        case MOpcode::CALL:
            // Calls may use all argument registers.  Mark GPR and FP argument
            // registers live so DCE does not delete their setup instructions.
            // The FP register set is ABI-dependent:
            //   SysV:  XMM0-XMM7 (up to 8 FP args in registers)
            //   Win64: XMM0-XMM3 (up to 4 FP args in registers)
#if defined(_WIN32)
            // Win64 GPR args: RCX, RDX, R8, R9
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RCX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RDX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::R8));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::R9));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
            // Win64 FP args: XMM0-XMM3
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM0));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM1));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM2));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM3));
#else
            // SysV GPR args: RDI, RSI, RDX, RCX, R8, R9
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RDI));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RSI));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RDX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RCX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::R8));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::R9));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
            // SysV FP args: XMM0-XMM7
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM0));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM1));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM2));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM3));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM4));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM5));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM6));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM7));
#endif
            // RAX holds the FP arg count for SysV varargs
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
            break;

        case MOpcode::RET:
            // Return uses RAX (or XMM0 for floats)
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::XMM0));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
            break;

        case MOpcode::CQO:
            // CQO sign-extends RAX into RDX:RAX — implicitly reads RAX
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
            break;

        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
            // IDIV/DIV divides RDX:RAX by the explicit operand — implicitly reads RAX and RDX
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
            usedRegs.insert(static_cast<uint16_t>(PhysReg::RDX));
            // Also add the explicit divisor operand
            if (instr.operands.size() >= 1)
                addIfPhysReg(instr.operands[0]);
            break;

        default:
            // For other instructions, conservatively assume all operand registers are used
            for (const auto &op : instr.operands) {
                addIfPhysReg(op);
                addMemRegs(op);
            }
            break;
    }
}

/// @brief Traits for the shared DCE template (x86-64 backend).
struct X64DCETraits {
    using MInstr = ::viper::codegen::x64::MInstr;
    using RegKey = uint16_t;

    static constexpr bool kIterateToFixpoint = true;

    static bool hasSideEffects(const ::viper::codegen::x64::MInstr &instr) noexcept {
        return dceHasSideEffects(instr);
    }

    static std::optional<RegKey> getDefRegKey(const MInstr &instr) noexcept {
        return getDefReg(instr);
    }

    static void collectUsedRegKeys(const MInstr &instr, std::unordered_set<RegKey> &live) noexcept {
        collectUsedRegs(instr, live);
    }

    static void addBlockExitLiveKeys(std::unordered_set<RegKey> &live) noexcept {
        const auto &exitRegs = getBlockExitLiveRegs();
        live.insert(exitRegs.begin(), exitRegs.end());
    }

    static bool isLabelOrBranchTarget(const MInstr &instr) noexcept {
        return instr.opcode == MOpcode::LABEL;
    }

    static void addAllAllocatableKeys(std::unordered_set<RegKey> &live) noexcept {
        const auto &allRegs = getAllAllocatableRegs();
        live.insert(allRegs.begin(), allRegs.end());
    }
};

} // namespace

std::size_t runBlockDCE(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    std::size_t eliminated = viper::codegen::common::runBlockDCE<X64DCETraits>(instrs);
    stats.deadCodeEliminated += eliminated;
    return eliminated;
}

} // namespace viper::codegen::x64::peephole
