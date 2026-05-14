//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/peephole/CopyPropDCE.cpp
// Purpose: Copy propagation, dead code elimination, dead FP store elimination,
//          dead flag-setter removal, and compute-into-target folding for the
//          AArch64 peephole optimizer.
//
// Key invariants:
//   - Copy propagation does not propagate through ABI registers.
//   - DCE conservatively marks callee-saved and ABI registers as live at exit.
//
// Ownership/Lifetime:
//   - Operates on mutable instruction vectors owned by the caller.
//
// Links: codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "CopyPropDCE.hpp"

#include "PeepholeCommon.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::aarch64::peephole {

std::size_t propagateCopies(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    std::unordered_map<uint32_t, MOperand> copyOrigin;
    std::size_t propagated = 0;

    auto invalidateDependents = [&copyOrigin](uint32_t originKey) {
        std::vector<uint32_t> toErase;
        for (const auto &[key, origin] : copyOrigin) {
            if (regKey(origin) == originKey)
                toErase.push_back(key);
        }
        for (uint32_t key : toErase)
            copyOrigin.erase(key);
    };

    for (auto &instr : instrs) {
        if (instr.opc == MOpcode::Br || instr.opc == MOpcode::BCond || instr.opc == MOpcode::Ret ||
            instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr) {
            if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr)
                copyOrigin.clear();
            continue;
        }

        // Shared copy-tracking for MovRR and FMovRR (same-class).
        auto trackCopy = [&](MInstr &mi) -> bool {
            const MOperand &dst = mi.ops[0];
            const MOperand &src = mi.ops[1];
            uint32_t dstKey = regKey(dst);

            invalidateDependents(dstKey);
            copyOrigin.erase(dstKey);

            MOperand origin = src;
            if (!isABIReg(src)) {
                auto it = copyOrigin.find(regKey(src));
                if (it != copyOrigin.end())
                    origin = it->second;
            }

            if (!samePhysReg(dst, origin)) {
                copyOrigin[dstKey] = origin;
                if (!samePhysReg(src, origin)) {
                    mi.ops[1] = origin;
                    ++propagated;
                }
            }
            return true;
        };

        // MovRR copy tracking
        if (instr.opc == MOpcode::MovRR && instr.ops.size() == 2 && isPhysReg(instr.ops[0]) &&
            isPhysReg(instr.ops[1])) {
            trackCopy(instr);
            continue;
        }

        // FMovRR copy tracking (same-class only)
        if (instr.opc == MOpcode::FMovRR && instr.ops.size() == 2 && isPhysReg(instr.ops[0]) &&
            isPhysReg(instr.ops[1]) && instr.ops[0].reg.cls == instr.ops[1].reg.cls) {
            trackCopy(instr);
            continue;
        }

        // Propagate uses BEFORE invalidating defs
        for (std::size_t i = 0; i < instr.ops.size(); ++i) {
            auto &op = instr.ops[i];
            if (!isPhysReg(op))
                continue;

            auto [isUse, isDef] = classifyOperand(instr, i);
            if (isUse && !isDef && !isABIReg(op)) {
                uint32_t key = regKey(op);
                auto it = copyOrigin.find(key);
                if (it != copyOrigin.end() && !samePhysReg(op, it->second)) {
                    op = it->second;
                    ++propagated;
                }
            }
        }

        // Invalidate definitions
        for (std::size_t i = 0; i < instr.ops.size(); ++i) {
            const auto &op = instr.ops[i];
            if (!isPhysReg(op))
                continue;

            auto [isUse, isDef] = classifyOperand(instr, i);
            if (isDef) {
                uint32_t key = regKey(op);
                invalidateDependents(key);
                copyOrigin.erase(key);
            }
        }
    }

    stats.copiesPropagated += static_cast<int>(propagated);
    return propagated;
}

std::size_t removeDeadInstructions(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    if (instrs.empty())
        return 0;

    std::unordered_set<uint32_t> liveRegs;

    // Mark argument registers as live at block exit
    for (int i = 0; i <= 7; ++i) {
        liveRegs.insert((static_cast<uint32_t>(RegClass::GPR) << 16) |
                        static_cast<uint32_t>(PhysReg::X0) + i);
    }
    for (int i = 0; i <= 7; ++i) {
        liveRegs.insert((static_cast<uint32_t>(RegClass::FPR) << 16) |
                        static_cast<uint32_t>(PhysReg::V0) + i);
    }

    // Mark callee-saved GPRs (x19-x28) as live at block exit
    for (uint32_t r = static_cast<uint32_t>(PhysReg::X19); r <= static_cast<uint32_t>(PhysReg::X28);
         ++r) {
        liveRegs.insert((static_cast<uint32_t>(RegClass::GPR) << 16) | r);
    }

    std::vector<bool> toRemove(instrs.size(), false);
    std::size_t removed = 0;

    for (std::size_t i = instrs.size(); i > 0; --i) {
        const std::size_t idx = i - 1;
        const auto &instr = instrs[idx];

        if (hasSideEffects(instr)) {
            for (std::size_t j = 0; j < instr.ops.size(); ++j) {
                const auto &op = instr.ops[j];
                if (isPhysReg(op)) {
                    auto [isUse, isDef] = classifyOperand(instr, j);
                    if (isUse)
                        liveRegs.insert(regKey(op));
                }
            }
            continue;
        }

        auto defReg = getDefinedReg(instr);
        if (defReg) {
            uint32_t key = regKey(*defReg);
            if (liveRegs.find(key) == liveRegs.end()) {
                toRemove[idx] = true;
                ++removed;
                continue;
            }

            liveRegs.erase(key);
        }

        for (std::size_t j = 0; j < instr.ops.size(); ++j) {
            const auto &op = instr.ops[j];
            if (isPhysReg(op)) {
                auto [isUse, isDef] = classifyOperand(instr, j);
                if (isUse)
                    liveRegs.insert(regKey(op));
            }
        }
    }

    if (removed > 0) {
        removeMarkedInstructions(instrs, toRemove);
        stats.deadInstructionsRemoved += static_cast<int>(removed);
    }

    return removed;
}

namespace {

[[nodiscard]] uint32_t physRegKey(PhysReg reg) noexcept {
    const RegClass cls = isGPR(reg) ? RegClass::GPR : RegClass::FPR;
    return (static_cast<uint32_t>(cls) << 16) | static_cast<uint32_t>(reg);
}

struct RegSet {
    uint64_t bits{0};

    void insertBit(unsigned bit) noexcept {
        bits |= (uint64_t{1} << bit);
    }

    void insertKey(uint32_t key) noexcept {
        const uint32_t cls = key >> 16;
        const uint32_t phys = key & 0xFFFFu;
        if (cls == static_cast<uint32_t>(RegClass::GPR)) {
            if (phys <= static_cast<uint32_t>(PhysReg::SP))
                insertBit(phys);
            return;
        }
        const uint32_t v0 = static_cast<uint32_t>(PhysReg::V0);
        if (phys >= v0 && phys <= static_cast<uint32_t>(PhysReg::V31))
            insertBit(32u + (phys - v0));
    }

    void eraseKey(uint32_t key) noexcept {
        const uint32_t cls = key >> 16;
        const uint32_t phys = key & 0xFFFFu;
        unsigned bit = 64;
        if (cls == static_cast<uint32_t>(RegClass::GPR)) {
            if (phys <= static_cast<uint32_t>(PhysReg::SP))
                bit = phys;
        } else {
            const uint32_t v0 = static_cast<uint32_t>(PhysReg::V0);
            if (phys >= v0 && phys <= static_cast<uint32_t>(PhysReg::V31))
                bit = 32u + (phys - v0);
        }
        if (bit < 64)
            bits &= ~(uint64_t{1} << bit);
    }

    [[nodiscard]] bool containsKey(uint32_t key) const noexcept {
        const uint32_t cls = key >> 16;
        const uint32_t phys = key & 0xFFFFu;
        unsigned bit = 64;
        if (cls == static_cast<uint32_t>(RegClass::GPR)) {
            if (phys <= static_cast<uint32_t>(PhysReg::SP))
                bit = phys;
        } else {
            const uint32_t v0 = static_cast<uint32_t>(PhysReg::V0);
            if (phys >= v0 && phys <= static_cast<uint32_t>(PhysReg::V31))
                bit = 32u + (phys - v0);
        }
        return bit < 64 && (bits & (uint64_t{1} << bit)) != 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return bits == 0;
    }
};

/// @brief Seed @p regs with the registers that are live at every function exit.
/// @details The set includes the ABI return register (int and FP), the stack
///          pointer, and every callee-saved register. Live-out at exit ensures
///          DCE does not delete the final write to any of these.
/// @param target Target ABI providing return + callee-saved register sets.
/// @param regs   Set to which the live-at-exit register keys are added.
void addTargetExitLive(const TargetInfo &target, std::unordered_set<uint32_t> &regs) {
    regs.insert(physRegKey(target.intReturnReg));
    regs.insert(physRegKey(target.f64ReturnReg));
    regs.insert(physRegKey(PhysReg::SP));
    for (PhysReg reg : target.calleeSavedGPR)
        regs.insert(physRegKey(reg));
    for (PhysReg reg : target.calleeSavedFPR)
        regs.insert(physRegKey(reg));
}

/// @brief `RegSet` overload of @ref addTargetExitLive for the bitset live-set path.
/// @param target Target ABI providing return + callee-saved register sets.
/// @param regs   RegSet to which the live-at-exit register keys are added.
void addTargetExitLive(const TargetInfo &target, RegSet &regs) {
    regs.insertKey(physRegKey(target.intReturnReg));
    regs.insertKey(physRegKey(target.f64ReturnReg));
    regs.insertKey(physRegKey(PhysReg::SP));
    for (PhysReg reg : target.calleeSavedGPR)
        regs.insertKey(physRegKey(reg));
    for (PhysReg reg : target.calleeSavedFPR)
        regs.insertKey(physRegKey(reg));
}

/// @brief Add the implicit-use register set of a call site to @p uses.
/// @details A call implicitly uses every register that holds an argument
///          (the architectural arg-passing GPRs and FPRs) plus the stack
///          pointer (since outgoing stack args live above SP).
/// @param target Target ABI providing the arg-register order.
/// @param uses   Set to which the implicit-use register keys are added.
void addCallImplicitUses(const TargetInfo &target, std::unordered_set<uint32_t> &uses) {
    for (PhysReg reg : target.intArgOrder)
        uses.insert(physRegKey(reg));
    for (PhysReg reg : target.f64ArgOrder)
        uses.insert(physRegKey(reg));
    uses.insert(physRegKey(PhysReg::SP));
}

/// @brief Add the implicit-clobber register set of a call site to @p defs.
/// @details A call clobbers every caller-saved register (the callee may
///          freely overwrite them). Liveness uses this to invalidate any
///          values held in caller-saved registers across the call boundary.
/// @param target Target ABI providing the caller-saved sets.
/// @param defs   Set to which the clobbered register keys are added.
void addCallClobbers(const TargetInfo &target, std::unordered_set<uint32_t> &defs) {
    for (PhysReg reg : target.callerSavedGPR)
        defs.insert(physRegKey(reg));
    for (PhysReg reg : target.callerSavedFPR)
        defs.insert(physRegKey(reg));
}

/// @brief `RegSet` overload of @ref addCallImplicitUses.
/// @param target Target ABI providing the arg-register order.
/// @param uses   RegSet to which the implicit-use register keys are added.
void addCallImplicitUses(const TargetInfo &target, RegSet &uses) {
    for (PhysReg reg : target.intArgOrder)
        uses.insertKey(physRegKey(reg));
    for (PhysReg reg : target.f64ArgOrder)
        uses.insertKey(physRegKey(reg));
    uses.insertKey(physRegKey(PhysReg::SP));
}

/// @brief `RegSet` overload of @ref addCallClobbers.
/// @param target Target ABI providing the caller-saved sets.
/// @param defs   RegSet to which the clobbered register keys are added.
void addCallClobbers(const TargetInfo &target, RegSet &defs) {
    for (PhysReg reg : target.callerSavedGPR)
        defs.insertKey(physRegKey(reg));
    for (PhysReg reg : target.callerSavedFPR)
        defs.insertKey(physRegKey(reg));
}

/// @brief Compute the use and def register sets for a single MIR instruction.
/// @details Walks @p instr's operands and consults `classifyOperand` to
///          classify each as a use, def, or both. Call instructions
///          (`Bl`/`Blr`) additionally pick up the call-implicit-uses and
///          call-clobbers from the ABI metadata.
/// @param instr  Machine instruction whose live-set contribution is computed.
/// @param target Target ABI for call-implicit handling.
/// @param uses   Set receiving the use register keys.
/// @param defs   Set receiving the def register keys.
void collectUsesDefs(const MInstr &instr,
                     const TargetInfo &target,
                     std::unordered_set<uint32_t> &uses,
                     std::unordered_set<uint32_t> &defs) {
    for (std::size_t idx = 0; idx < instr.ops.size(); ++idx) {
        const auto [isUse, isDef] = classifyOperand(instr, idx);
        const auto &op = instr.ops[idx];
        if (!isPhysReg(op))
            continue;
        const uint32_t key = regKey(op);
        if (isUse)
            uses.insert(key);
        if (isDef)
            defs.insert(key);
    }

    if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr) {
        addCallImplicitUses(target, uses);
        addCallClobbers(target, defs);
    }
}

/// @brief `RegSet` overload of @ref collectUsesDefs for the bitset live-set path.
/// @param instr  Machine instruction whose live-set contribution is computed.
/// @param target Target ABI for call-implicit handling.
/// @param uses   RegSet receiving the use register keys.
/// @param defs   RegSet receiving the def register keys.
void collectUsesDefs(const MInstr &instr, const TargetInfo &target, RegSet &uses, RegSet &defs) {
    for (std::size_t idx = 0; idx < instr.ops.size(); ++idx) {
        const auto [isUse, isDef] = classifyOperand(instr, idx);
        const auto &op = instr.ops[idx];
        if (!isPhysReg(op))
            continue;
        const uint32_t key = regKey(op);
        if (isUse)
            uses.insertKey(key);
        if (isDef)
            defs.insertKey(key);
    }

    if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr) {
        addCallImplicitUses(target, uses);
        addCallClobbers(target, defs);
    }
}

/// @brief Append the block index for @p label to @p succs if not already present.
/// @details Looks @p label up in the label-to-index map and appends the
///          corresponding block index; duplicates are skipped. Used while
///          building per-block successor lists for the liveness CFG walk.
/// @param succs        Successor list being built (modified in place).
/// @param labelToIndex Pre-built map from block name to block index.
/// @param label        Branch target label to add as a successor.
void addUniqueSucc(std::vector<std::size_t> &succs,
                   const std::unordered_map<std::string, std::size_t> &labelToIndex,
                   const std::string &label) {
    const auto it = labelToIndex.find(label);
    if (it == labelToIndex.end())
        return;
    if (std::find(succs.begin(), succs.end(), it->second) == succs.end())
        succs.push_back(it->second);
}

/// @brief Test whether @p instr is a conditional branch (`B.cond`/`CBZ`/`CBNZ`).
/// @param instr Machine instruction to classify.
/// @return True if @p instr's opcode is one of the conditional-branch forms.
[[nodiscard]] bool isConditionalBranch(const MInstr &instr) noexcept {
    return instr.opc == MOpcode::BCond || instr.opc == MOpcode::Cbz || instr.opc == MOpcode::Cbnz;
}

/// @brief Test whether @p opcode writes the NZCV flags.
/// @details Used by DCE to refuse to delete instructions whose only observable
///          side effect is the flag write that a downstream conditional branch
///          will consume. Covers the comparison family and the flag-setting
///          arithmetic variants (`ADDS`/`SUBS`/`ANDS`).
/// @param opcode Opcode to classify.
/// @return True if @p opcode sets the NZCV flags.
[[nodiscard]] bool setsFlagsForDCE(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::CmpRR:
        case MOpcode::CmpRI:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
        case MOpcode::AddsRRR:
        case MOpcode::SubsRRR:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] std::vector<std::vector<std::size_t>> buildSuccessors(const MFunction &fn) {
    std::unordered_map<std::string, std::size_t> labelToIndex;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
        labelToIndex.emplace(fn.blocks[i].name, i);

    std::vector<std::vector<std::size_t>> succs(fn.blocks.size());
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        const auto addFallthrough = [&]() {
            if (bi + 1 < fn.blocks.size())
                succs[bi].push_back(bi + 1);
        };

        const auto &instrs = fn.blocks[bi].instrs;
        if (instrs.empty()) {
            addFallthrough();
            continue;
        }

        const auto &last = instrs.back();
        if (last.opc == MOpcode::Br && !last.ops.empty() &&
            last.ops[0].kind == MOperand::Kind::Label) {
            if (instrs.size() >= 2) {
                const auto &prev = instrs[instrs.size() - 2];
                if (isConditionalBranch(prev) && prev.ops.size() >= 2 &&
                    prev.ops[1].kind == MOperand::Kind::Label)
                    addUniqueSucc(succs[bi], labelToIndex, prev.ops[1].label);
            }
            addUniqueSucc(succs[bi], labelToIndex, last.ops[0].label);
            continue;
        }

        if (isConditionalBranch(last) && last.ops.size() >= 2 &&
            last.ops[1].kind == MOperand::Kind::Label) {
            addUniqueSucc(succs[bi], labelToIndex, last.ops[1].label);
            addFallthrough();
            continue;
        }

        if (last.opc != MOpcode::Ret)
            addFallthrough();
    }
    return succs;
}

} // namespace

std::size_t removeDeadInstructionsCFG(MFunction &fn,
                                      PeepholeStats &stats,
                                      const TargetInfo &target) {
    if (fn.blocks.empty())
        return 0;

    const auto successors = buildSuccessors(fn);
    const std::size_t blockCount = fn.blocks.size();

    std::vector<RegSet> gen(blockCount);
    std::vector<RegSet> kill(blockCount);
    std::vector<RegSet> liveIn(blockCount);
    std::vector<RegSet> liveOut(blockCount);

    for (std::size_t bi = 0; bi < blockCount; ++bi) {
        for (const auto &instr : fn.blocks[bi].instrs) {
            RegSet uses;
            RegSet defs;
            collectUsesDefs(instr, target, uses, defs);

            gen[bi].bits |= uses.bits & ~kill[bi].bits;
            kill[bi].bits |= defs.bits;
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t bi = blockCount; bi-- > 0;) {
            RegSet newOut;
            if (successors[bi].empty()) {
                addTargetExitLive(target, newOut);
            } else {
                for (std::size_t succ : successors[bi])
                    newOut.bits |= liveIn[succ].bits;
            }

            RegSet newIn;
            newIn.bits = gen[bi].bits | (newOut.bits & ~kill[bi].bits);

            if (newOut.bits != liveOut[bi].bits || newIn.bits != liveIn[bi].bits) {
                liveOut[bi] = newOut;
                liveIn[bi] = newIn;
                changed = true;
            }
        }
    }

    std::size_t removed = 0;
    for (std::size_t bi = 0; bi < blockCount; ++bi) {
        auto &instrs = fn.blocks[bi].instrs;
        if (instrs.empty())
            continue;

        RegSet live = liveOut[bi];
        std::vector<bool> toRemove(instrs.size(), false);

        for (std::size_t i = instrs.size(); i-- > 0;) {
            const auto &instr = instrs[i];
            RegSet uses;
            RegSet defs;
            collectUsesDefs(instr, target, uses, defs);

            const bool defLive = (defs.bits & live.bits) != 0;

            if (!defs.empty() && !defLive && !hasSideEffects(instr) &&
                !setsFlagsForDCE(instr.opc)) {
                toRemove[i] = true;
                ++removed;
                continue;
            }

            live.bits &= ~defs.bits;
            live.bits |= uses.bits;
        }

        if (std::any_of(toRemove.begin(), toRemove.end(), [](bool value) { return value; }))
            removeMarkedInstructions(instrs, toRemove);
    }

    stats.deadInstructionsRemoved += static_cast<int>(removed);
    return removed;
}

std::size_t eliminateDeadFpStores(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    std::unordered_map<int64_t, std::size_t> lastStore;
    std::vector<bool> toRemove(instrs.size(), false);
    std::size_t removed = 0;

    for (std::size_t i = 0; i < instrs.size(); ++i) {
        const auto &instr = instrs[i];

        if ((instr.opc == MOpcode::StrRegFpImm || instr.opc == MOpcode::StrFprFpImm) &&
            instr.ops.size() >= 2 && instr.ops[1].kind == MOperand::Kind::Imm) {
            const int64_t offset = instr.ops[1].imm;
            auto it = lastStore.find(offset);
            if (it != lastStore.end()) {
                toRemove[it->second] = true;
                ++removed;
            }
            lastStore[offset] = i;
            continue;
        }

        if ((instr.opc == MOpcode::LdrRegFpImm || instr.opc == MOpcode::LdrFprFpImm) &&
            instr.ops.size() >= 2 && instr.ops[1].kind == MOperand::Kind::Imm) {
            lastStore.erase(instr.ops[1].imm);
            continue;
        }

        if ((instr.opc == MOpcode::StpRegFpImm || instr.opc == MOpcode::StpFprFpImm) &&
            instr.ops.size() >= 3 && instr.ops[2].kind == MOperand::Kind::Imm) {
            const int64_t off = instr.ops[2].imm;
            lastStore.erase(off);
            lastStore.erase(off + 8);
            continue;
        }
        if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
            instr.ops.size() >= 3 && instr.ops[2].kind == MOperand::Kind::Imm) {
            lastStore.erase(instr.ops[2].imm);
            lastStore.erase(instr.ops[2].imm + 8);
            continue;
        }

        if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr) {
            lastStore.clear();
            continue;
        }

        if (instr.opc == MOpcode::Br || instr.opc == MOpcode::BCond || instr.opc == MOpcode::Ret ||
            instr.opc == MOpcode::Cbz || instr.opc == MOpcode::Cbnz) {
            lastStore.clear();
            continue;
        }
    }

    if (removed > 0) {
        removeMarkedInstructions(instrs, toRemove);
        stats.deadInstructionsRemoved += static_cast<int>(removed);
    }
    return removed;
}

std::size_t removeDeadFlagSetters(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    auto setsFlags = [](MOpcode opc) -> bool {
        switch (opc) {
            case MOpcode::CmpRR:
            case MOpcode::CmpRI:
            case MOpcode::TstRR:
            case MOpcode::FCmpRR:
            case MOpcode::AddsRRR:
            case MOpcode::SubsRRR:
            case MOpcode::AddsRI:
            case MOpcode::SubsRI:
                return true;
            default:
                return false;
        }
    };

    auto readsFlags = [](MOpcode opc) -> bool {
        switch (opc) {
            case MOpcode::BCond:
            case MOpcode::Cset:
            case MOpcode::Csel:
                return true;
            default:
                return false;
        }
    };

    std::vector<bool> toRemove(instrs.size(), false);
    std::size_t removed = 0;

    for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
        if (!setsFlags(instrs[i].opc))
            continue;

        bool isDead = false;
        for (std::size_t j = i + 1; j < instrs.size(); ++j) {
            if (readsFlags(instrs[j].opc))
                break;
            if (setsFlags(instrs[j].opc)) {
                if (instrs[i].opc == MOpcode::CmpRI || instrs[i].opc == MOpcode::CmpRR ||
                    instrs[i].opc == MOpcode::TstRR || instrs[i].opc == MOpcode::FCmpRR) {
                    isDead = true;
                }
                break;
            }
        }
        if (isDead) {
            toRemove[i] = true;
            ++removed;
        }
    }

    if (removed > 0) {
        removeMarkedInstructions(instrs, toRemove);
        stats.deadInstructionsRemoved += static_cast<int>(removed);
    }
    return removed;
}

std::size_t foldComputeIntoTarget(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    auto isSimpleALU = [](MOpcode opc) -> bool {
        switch (opc) {
            case MOpcode::AddRRR:
            case MOpcode::SubRRR:
            case MOpcode::AddRI:
            case MOpcode::SubRI:
            case MOpcode::AddsRRR:
            case MOpcode::SubsRRR:
            case MOpcode::AddsRI:
            case MOpcode::SubsRI:
            case MOpcode::MulRRR:
            case MOpcode::SmulhRRR:
            case MOpcode::UmulhRRR:
            case MOpcode::AndRRR:
            case MOpcode::OrrRRR:
            case MOpcode::EorRRR:
            case MOpcode::LslRI:
            case MOpcode::LsrRI:
            case MOpcode::AsrRI:
                return true;
            default:
                return false;
        }
    };

    std::size_t folded = 0;
    for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
        if (!isSimpleALU(instrs[i].opc))
            continue;
        if (instrs[i].ops.empty() || !isPhysReg(instrs[i].ops[0]))
            continue;

        const MOperand aluDst = instrs[i].ops[0];

        std::size_t movIdx = i + 1;
        while (movIdx < instrs.size() && instrs[movIdx].opc == MOpcode::BCond)
            ++movIdx;

        if (movIdx >= instrs.size())
            continue;
        if (instrs[movIdx].opc != MOpcode::MovRR)
            continue;
        if (instrs[movIdx].ops.size() != 2)
            continue;
        if (!samePhysReg(instrs[movIdx].ops[1], aluDst))
            continue;
        if (!isPhysReg(instrs[movIdx].ops[0]))
            continue;

        const MOperand movDst = instrs[movIdx].ops[0];

        bool interveningUse = false;
        for (std::size_t k = i + 1; k < movIdx; ++k) {
            if (usesReg(instrs[k], movDst)) {
                interveningUse = true;
                break;
            }
        }
        if (interveningUse)
            continue;

        bool aluDstDead = true;
        for (std::size_t j = movIdx + 1; j < instrs.size(); ++j) {
            if (usesReg(instrs[j], aluDst)) {
                aluDstDead = false;
                break;
            }
            if (definesReg(instrs[j], aluDst))
                break;
        }
        if (!aluDstDead)
            continue;

        instrs[i].ops[0] = movDst;
        instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(movIdx));
        ++folded;
        ++stats.deadInstructionsRemoved;

        if (i > 0)
            --i;
    }
    return folded;
}

std::size_t eliminateDeadFpStoresCrossBlock(MFunction &fn, PeepholeStats &stats) {
    // Only compiler-created spill slots are safe for whole-function dead-store
    // removal. FP-relative locals/allocas can be observed through address-derived
    // base-register loads even when no direct LdrRegFpImm remains.
    std::unordered_set<int64_t> eligibleOffsets;
    for (const auto &slot : fn.frame.spills) {
        for (int byte = 0; byte < slot.size; byte += 8)
            eligibleOffsets.insert(static_cast<int64_t>(slot.offset + byte));
    }
    if (eligibleOffsets.empty())
        return 0;

    // Step 1: Collect all FP offsets that are LOADED anywhere in the function.
    std::unordered_set<int64_t> loadedOffsets;
    for (const auto &bb : fn.blocks) {
        for (const auto &mi : bb.instrs) {
            if (mi.opc == MOpcode::LdrRegFpImm && mi.ops.size() >= 2 &&
                mi.ops[1].kind == MOperand::Kind::Imm) {
                loadedOffsets.insert(mi.ops[1].imm);
            }
            if (mi.opc == MOpcode::LdpRegFpImm && mi.ops.size() >= 3 &&
                mi.ops[2].kind == MOperand::Kind::Imm) {
                loadedOffsets.insert(mi.ops[2].imm);
                loadedOffsets.insert(mi.ops[2].imm + 8);
            }
        }
    }

    // Step 2: Remove stores to offsets that are never loaded.
    std::size_t removed = 0;
    for (auto &bb : fn.blocks) {
        bb.instrs.erase(std::remove_if(bb.instrs.begin(),
                                       bb.instrs.end(),
                                       [&](const MInstr &mi) {
                                           if (mi.opc == MOpcode::StrRegFpImm &&
                                               mi.ops.size() >= 2 &&
                                               mi.ops[1].kind == MOperand::Kind::Imm &&
                                               eligibleOffsets.count(mi.ops[1].imm) != 0 &&
                                               !loadedOffsets.count(mi.ops[1].imm)) {
                                               ++removed;
                                               return true;
                                           }
                                           return false;
                                       }),
                        bb.instrs.end());
    }
    stats.deadInstructionsRemoved += static_cast<int>(removed);
    return removed;
}

} // namespace viper::codegen::aarch64::peephole
