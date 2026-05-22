//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/peephole/DCE.cpp
// Purpose: Dead code elimination peephole sub-pass for the x86-64 backend.
//          Defines x86-64 specific traits for the shared DCE template in
//          PeepholeDCE.hpp.
// Key invariants:
//   - RSP modifications are never eliminated (stack frame changes).
//   - Iterates to a fixed point within each basic block.
//   - Division instructions (IDIV/DIV) are treated as side-effecting.
// Ownership/Lifetime:
//   - Stateless; delegates to the shared DCE template.
// Links: codegen/x86_64/peephole/DCE.hpp,
//        codegen/x86_64/peephole/PeepholeCommon.hpp,
//        codegen/common/PeepholeDCE.hpp
//
//===----------------------------------------------------------------------===//

#include "DCE.hpp"

#include "codegen/x86_64/OperandRoles.hpp"

#include <cstdint>
#include <optional>

namespace viper::codegen::x64::peephole {
namespace {

using RegMask = uint64_t;

[[nodiscard]] RegMask regBit(uint16_t reg) noexcept {
    return reg < 64 ? (RegMask{1} << reg) : RegMask{0};
}

void addReg(RegMask &mask, uint16_t reg) noexcept {
    mask |= regBit(reg);
}

[[nodiscard]] bool containsReg(RegMask mask, uint16_t reg) noexcept {
    return (mask & regBit(reg)) != 0;
}

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
    return hasObservableSideEffects(instr.opcode);
}

/// @brief Get the destination register from an instruction, if it defines one.
[[nodiscard]] std::optional<uint16_t> getDefReg(const MInstr &instr) noexcept {
    if (instr.operands.empty())
        return std::nullopt;

    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isUse;
        if (!isDef)
            continue;
        const auto *reg = std::get_if<OpReg>(&instr.operands[idx]);
        if (reg && reg->isPhys)
            return reg->idOrPhys;
    }
    return std::nullopt;
}

/// @brief Collect all physical registers used by an instruction.
[[nodiscard]] RegMask collectUsedRegs(const MInstr &instr) {
    RegMask usedRegs = 0;

    // Helper to add a register if it's physical
    auto addIfPhysReg = [&usedRegs](const Operand &op) {
        const auto *reg = std::get_if<OpReg>(&op);
        if (reg && reg->isPhys)
            addReg(usedRegs, reg->idOrPhys);
    };

    // Helper to add registers from memory operand
    auto addMemRegs = [&usedRegs](const Operand &op) {
        const auto *mem = std::get_if<OpMem>(&op);
        if (mem) {
            // Base register is always valid in OpMem
            if (mem->base.isPhys)
                addReg(usedRegs, mem->base.idOrPhys);
            // Index register is only valid when hasIndex is true
            if (mem->hasIndex && mem->index.isPhys)
                addReg(usedRegs, mem->index.idOrPhys);
        }
    };

    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isDef;
        if (!isUse)
            continue;
        addIfPhysReg(instr.operands[idx]);
        addMemRegs(instr.operands[idx]);
    }

    return usedRegs;
}

/// @brief Mark all registers a CALL implicitly uses as live.
/// @details Argument registers, plus RAX (vararg vector-arg count for SysV),
///          plus RSP must stay live across CALL points so DCE cannot drop
///          the instructions that populate them.
void addCallUsedRegs(const TargetInfo &target, RegMask &usedRegs) {
    for (std::size_t i = 0; i < target.maxGPRArgs && i < target.intArgOrder.size(); ++i)
        addReg(usedRegs, static_cast<uint16_t>(target.intArgOrder[i]));
    for (std::size_t i = 0; i < target.maxFPArgs && i < target.f64ArgOrder.size(); ++i)
        addReg(usedRegs, static_cast<uint16_t>(target.f64ArgOrder[i]));
    addReg(usedRegs, static_cast<uint16_t>(PhysReg::RSP));
    // SysV varargs use AL to carry the number of vector arguments. Keeping RAX
    // live at calls is conservative for non-varargs and required for varargs.
    addReg(usedRegs, static_cast<uint16_t>(PhysReg::RAX));
}

/// @brief Mark RET-implicit registers as live.
/// @details Return value registers (int + fp), the stack pointer, and all
///          callee-saved registers must survive to the function epilogue.
void addReturnUsedRegs(const TargetInfo &target, RegMask &usedRegs) {
    addReg(usedRegs, static_cast<uint16_t>(target.intReturnReg));
    addReg(usedRegs, static_cast<uint16_t>(target.f64ReturnReg));
    addReg(usedRegs, static_cast<uint16_t>(PhysReg::RSP));
    for (PhysReg reg : target.calleeSavedGPR)
        addReg(usedRegs, static_cast<uint16_t>(reg));
    for (PhysReg reg : target.calleeSavedFPR)
        addReg(usedRegs, static_cast<uint16_t>(reg));
}

/// @brief Seed @p liveRegs with the registers conservatively live at block exit.
/// @details Equivalent to @ref addReturnUsedRegs but also explicitly
///          re-adds @c RSP so frame-manipulating blocks always keep stack
///          accounting alive.
void addExitLiveRegs(const TargetInfo &target, RegMask &liveRegs) {
    addReturnUsedRegs(target, liveRegs);
    addReg(liveRegs, static_cast<uint16_t>(PhysReg::RSP));
}

void addAllAllocatableRegs(RegMask &liveRegs) {
    for (uint16_t reg : getAllAllocatableRegs())
        addReg(liveRegs, reg);
}

/// @brief Add implicit register uses for @p instr to @p liveRegs / @p flagsLive.
/// @details Some opcodes touch registers that do not appear in their operand
///          list — CALL implicitly reads arg registers, RET reads the return
///          regs, CQO and IDIV/DIV read the RAX/RDX pair, and any opcode in
///          the EFLAGS-using set marks flags as live.
void collectImplicitUses(const MInstr &instr,
                         const TargetInfo &target,
                         RegMask &liveRegs,
                         bool &flagsLive) {
    if (usesEFlags(instr.opcode))
        flagsLive = true;

    switch (instr.opcode) {
        case MOpcode::CALL:
            addCallUsedRegs(target, liveRegs);
            break;
        case MOpcode::RET:
            addReturnUsedRegs(target, liveRegs);
            break;
        case MOpcode::CQO:
            addReg(liveRegs, static_cast<uint16_t>(PhysReg::RAX));
            break;
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
            addReg(liveRegs, static_cast<uint16_t>(PhysReg::RAX));
            addReg(liveRegs, static_cast<uint16_t>(PhysReg::RDX));
            break;
        default:
            break;
    }
}

} // namespace

/// @brief Run liveness-based dead-code elimination over a single block.
/// @details Performs a backward sweep maintaining a live-register set and a
///          flags-live flag. Instructions that define only dead registers
///          (and lack observable side effects) are marked for removal. The
///          loop iterates to a fixed point because removing one instruction
///          can make another dead. @p preservePhysRegsAtExit seeds the
///          initial live set with every allocatable register — used for
///          blocks whose successors aren't visible in this analysis (e.g.
///          when the function has no terminator yet).
/// @param instrs Block instructions, mutated in place.
/// @param stats Pass-wide statistics accumulator.
/// @param target Calling-convention metadata for implicit-use computation.
/// @param preservePhysRegsAtExit If true, keep every physical reg live.
/// @return Number of instructions eliminated.
std::size_t runBlockDCE(std::vector<MInstr> &instrs,
                        PeepholeStats &stats,
                        const TargetInfo &target,
                        bool preservePhysRegsAtExit) {
    if (instrs.empty())
        return 0;

    std::size_t eliminated = 0;

    RegMask liveRegs = 0;
    if (preservePhysRegsAtExit) {
        addAllAllocatableRegs(liveRegs);
        addReg(liveRegs, static_cast<uint16_t>(PhysReg::RSP));
    } else {
        addExitLiveRegs(target, liveRegs);
    }
    bool flagsLive = false;

    std::vector<bool> toRemove(instrs.size(), false);

    for (std::size_t i = instrs.size(); i-- > 0;) {
        const auto &instr = instrs[i];
        if (instr.opcode == MOpcode::LABEL)
            addAllAllocatableRegs(liveRegs);

        const RegMask explicitUses = collectUsedRegs(instr);
        const bool explicitFlagsUse = usesEFlags(instr.opcode);

        const auto defReg = getDefReg(instr);
        const bool definesFlags = definesEFlags(instr.opcode);
        const bool hasTrackedDef = defReg.has_value() || definesFlags;
        const bool regResultLive = defReg && containsReg(liveRegs, *defReg);
        const bool flagsResultLive = definesFlags && flagsLive;
        const bool anyResultLive = regResultLive || flagsResultLive;

        if (hasTrackedDef && !dceHasSideEffects(instr) && !anyResultLive) {
            toRemove[i] = true;
            ++eliminated;
            continue;
        }

        if (defReg)
            liveRegs &= ~regBit(*defReg);
        if (definesFlags)
            flagsLive = false;

        liveRegs |= explicitUses;
        collectImplicitUses(instr, target, liveRegs, flagsLive);
        if (explicitFlagsUse)
            flagsLive = true;
    }

    if (eliminated != 0)
        removeMarkedInstructions(instrs, toRemove);

    stats.deadCodeEliminated += eliminated;
    return eliminated;
}

} // namespace viper::codegen::x64::peephole
