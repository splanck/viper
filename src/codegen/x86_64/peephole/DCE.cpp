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

#include "codegen/x86_64/OperandRoles.hpp"

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

    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isDef;
        if (!isUse)
            continue;
        addIfPhysReg(instr.operands[idx]);
        addMemRegs(instr.operands[idx]);
    }
}

void addCallUsedRegs(const TargetInfo &target, std::unordered_set<uint16_t> &usedRegs) {
    for (std::size_t i = 0; i < target.maxGPRArgs && i < target.intArgOrder.size(); ++i)
        usedRegs.insert(static_cast<uint16_t>(target.intArgOrder[i]));
    for (std::size_t i = 0; i < target.maxFPArgs && i < target.f64ArgOrder.size(); ++i)
        usedRegs.insert(static_cast<uint16_t>(target.f64ArgOrder[i]));
    usedRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
    // SysV varargs use AL to carry the number of vector arguments. Keeping RAX
    // live at calls is conservative for non-varargs and required for varargs.
    usedRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
}

void addReturnUsedRegs(const TargetInfo &target, std::unordered_set<uint16_t> &usedRegs) {
    usedRegs.insert(static_cast<uint16_t>(target.intReturnReg));
    usedRegs.insert(static_cast<uint16_t>(target.f64ReturnReg));
    usedRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
    for (PhysReg reg : target.calleeSavedGPR)
        usedRegs.insert(static_cast<uint16_t>(reg));
    for (PhysReg reg : target.calleeSavedFPR)
        usedRegs.insert(static_cast<uint16_t>(reg));
}

void addExitLiveRegs(const TargetInfo &target, std::unordered_set<uint16_t> &liveRegs) {
    addReturnUsedRegs(target, liveRegs);
    liveRegs.insert(static_cast<uint16_t>(PhysReg::RSP));
}

void collectImplicitUses(const MInstr &instr,
                         const TargetInfo &target,
                         std::unordered_set<uint16_t> &liveRegs,
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
            liveRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
            break;
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
            liveRegs.insert(static_cast<uint16_t>(PhysReg::RAX));
            liveRegs.insert(static_cast<uint16_t>(PhysReg::RDX));
            break;
        default:
            break;
    }
}

} // namespace

std::size_t runBlockDCE(std::vector<MInstr> &instrs,
                        PeepholeStats &stats,
                        const TargetInfo &target) {
    if (instrs.empty())
        return 0;

    constexpr std::size_t kMaxDCEIterations = 100;
    std::size_t eliminated = 0;

    for (std::size_t iter = 0; iter < kMaxDCEIterations; ++iter) {
        std::unordered_set<uint16_t> liveRegs;
        addExitLiveRegs(target, liveRegs);
        bool flagsLive = false;

        std::vector<bool> toRemove(instrs.size(), false);
        std::size_t removedThisIter = 0;

        for (std::size_t i = instrs.size(); i-- > 0;) {
            const auto &instr = instrs[i];
            if (instr.opcode == MOpcode::LABEL) {
                const auto &allRegs = getAllAllocatableRegs();
                liveRegs.insert(allRegs.begin(), allRegs.end());
            }

            std::unordered_set<uint16_t> explicitUses;
            collectUsedRegs(instr, explicitUses);
            bool explicitFlagsUse = usesEFlags(instr.opcode);

            const auto defReg = getDefReg(instr);
            const bool definesFlags = definesEFlags(instr.opcode);
            const bool hasTrackedDef = defReg.has_value() || definesFlags;
            const bool regResultLive = defReg && liveRegs.count(*defReg) != 0;
            const bool flagsResultLive = definesFlags && flagsLive;
            const bool anyResultLive = regResultLive || flagsResultLive;

            if (hasTrackedDef && !dceHasSideEffects(instr) && !anyResultLive) {
                toRemove[i] = true;
                ++removedThisIter;
                continue;
            }

            if (defReg)
                liveRegs.erase(*defReg);
            if (definesFlags)
                flagsLive = false;

            liveRegs.insert(explicitUses.begin(), explicitUses.end());
            collectImplicitUses(instr, target, liveRegs, flagsLive);
            if (explicitFlagsUse)
                flagsLive = true;
        }

        if (removedThisIter == 0)
            break;

        removeMarkedInstructions(instrs, toRemove);
        eliminated += removedThisIter;
    }

    stats.deadCodeEliminated += eliminated;
    return eliminated;
}

} // namespace viper::codegen::x64::peephole
