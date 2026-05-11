//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/peephole/MemoryOpt.cpp
// Purpose: x86-64 memory access peephole optimizations: dead frame store
//          elimination and store-to-load forwarding.
// Key invariants:
//   - Only frame-relative (RBP-based) memory accesses are considered.
//   - Memory barriers (CALL, JMP, JCC, RET) flush all forwarding state.
// Ownership/Lifetime:
//   - Stateless; all state is owned by the caller.
// Links: codegen/x86_64/peephole/MemoryOpt.hpp,
//        codegen/x86_64/peephole/PeepholeCommon.hpp,
//        codegen/x86_64/OperandRoles.hpp
//
//===----------------------------------------------------------------------===//

#include "MemoryOpt.hpp"

#include "codegen/x86_64/OperandRoles.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>

namespace viper::codegen::x64::peephole {
namespace {

/// @brief Description of a frame-relative memory access used by the optimiser.
struct FrameAccess {
    int32_t disp{0};               ///< Signed displacement from %rbp.
    RegClass cls{RegClass::GPR};   ///< Whether the access uses GPR or XMM.
};

/// @brief Decode @p instr as a load from a %rbp-relative frame slot, if applicable.
/// @details Recognises @c MOVmr (GPR load) and @c MOVSDmr (XMM load) with a
///          pure base+disp memory operand whose base is the architectural
///          frame pointer. Anything more exotic (indexed, RIP-relative,
///          RSP-relative) is rejected to keep the analysis conservative.
/// @return The frame slot descriptor on success, @c std::nullopt otherwise.
std::optional<FrameAccess> frameLoad(const MInstr &instr) {
    if (instr.opcode != MOpcode::MOVmr && instr.opcode != MOpcode::MOVSDmr)
        return std::nullopt;
    if (instr.operands.size() < 2)
        return std::nullopt;
    const auto *mem = std::get_if<OpMem>(&instr.operands[1]);
    if (!mem || mem->hasIndex || !mem->base.isPhys ||
        static_cast<PhysReg>(mem->base.idOrPhys) != PhysReg::RBP)
        return std::nullopt;
    return FrameAccess{mem->disp, instr.opcode == MOpcode::MOVSDmr ? RegClass::XMM : RegClass::GPR};
}

/// @brief Companion to @ref frameLoad — decodes a frame-slot store.
/// @details Recognises @c MOVrm / @c MOVSDrm into a pure base+disp address
///          whose base is %rbp.
std::optional<FrameAccess> frameStore(const MInstr &instr) {
    if (instr.opcode != MOpcode::MOVrm && instr.opcode != MOpcode::MOVSDrm)
        return std::nullopt;
    if (instr.operands.size() < 2)
        return std::nullopt;
    const auto *mem = std::get_if<OpMem>(&instr.operands[0]);
    if (!mem || mem->hasIndex || !mem->base.isPhys ||
        static_cast<PhysReg>(mem->base.idOrPhys) != PhysReg::RBP)
        return std::nullopt;
    return FrameAccess{mem->disp, instr.opcode == MOpcode::MOVSDrm ? RegClass::XMM : RegClass::GPR};
}

/// @brief Predicate: do two frame accesses refer to the same typed slot?
/// @details Requires both the displacement and the register class to match.
bool sameFrameSlot(const FrameAccess &a, const FrameAccess &b) {
    return a.disp == b.disp && a.cls == b.cls;
}

/// @brief Predicate: do two frame accesses share the same byte address?
/// @details Ignores register class — used when a GPR store and an XMM
///          store would alias and the elder access becomes dead.
bool sameFrameAddress(const FrameAccess &a, const FrameAccess &b) {
    return a.disp == b.disp;
}

/// @brief Predicate: does @p instr re-define the physical register in @p regOperand?
/// @details Used by the store-forwarding logic to detect when the source
///          register of a recorded store has been clobbered before the
///          subsequent load can reuse it.
bool definesOperandReg(const MInstr &instr, const Operand &regOperand) {
    const auto *reg = std::get_if<OpReg>(&regOperand);
    if (!reg || !reg->isPhys)
        return false;
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isUse;
        if (!isDef)
            continue;
        const auto *def = std::get_if<OpReg>(&instr.operands[idx]);
        if (def && def->isPhys && def->cls == reg->cls && def->idOrPhys == reg->idOrPhys)
            return true;
    }
    return false;
}

/// @brief Predicate: must we discard tracked frame state before @p instr?
/// @details Returns true for control-flow boundaries (CALL/JMP/JCC/RET/
///          LABEL/UD2) and for any memory access that is not a known
///          frame load or frame store. The conservative treatment of
///          unknown memory ops keeps optimisations sound in the presence
///          of aliasing.
bool isMemoryBarrier(const MInstr &instr) {
    if (instr.opcode == MOpcode::CALL || instr.opcode == MOpcode::JMP ||
        instr.opcode == MOpcode::JCC || instr.opcode == MOpcode::RET ||
        instr.opcode == MOpcode::LABEL || instr.opcode == MOpcode::UD2)
        return true;

    const bool knownFrameLoad = frameLoad(instr).has_value();
    const bool knownFrameStore = frameStore(instr).has_value();
    if (knownFrameLoad || knownFrameStore)
        return false;

    for (const auto &op : instr.operands) {
        if (std::holds_alternative<OpMem>(op) || std::holds_alternative<OpRipLabel>(op))
            return true;
    }
    return false;
}

} // namespace

/// @brief Drop frame stores that are overwritten before any load reads them.
/// @details Walks the block forward tracking, per (slot, class), the index
///          of the most-recent unread store. When a fresh store hits the
///          same address, the prior store is marked dead. Loads, memory
///          barriers, and aliasing between GPR/XMM slot writes clear the
///          tracker so we never delete a live value.
/// @param instrs Block instructions, mutated in place.
/// @param stats Pass-wide statistics accumulator.
/// @return Number of dead stores eliminated.
std::size_t eliminateDeadFrameStores(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    std::unordered_map<int32_t, std::size_t> lastGprStore;
    std::unordered_map<int32_t, std::size_t> lastXmmStore;
    std::vector<bool> remove(instrs.size(), false);

    auto mapFor = [&](RegClass cls) -> std::unordered_map<int32_t, std::size_t> & {
        return cls == RegClass::XMM ? lastXmmStore : lastGprStore;
    };

    std::size_t removed = 0;
    for (std::size_t i = 0; i < instrs.size(); ++i) {
        if (auto store = frameStore(instrs[i])) {
            auto &stores = mapFor(store->cls);
            auto it = stores.find(store->disp);
            if (it != stores.end() && !remove[it->second]) {
                remove[it->second] = true;
                ++removed;
            }
            auto &otherStores =
                mapFor(store->cls == RegClass::XMM ? RegClass::GPR : RegClass::XMM);
            auto otherIt = otherStores.find(store->disp);
            if (otherIt != otherStores.end() && !remove[otherIt->second]) {
                remove[otherIt->second] = true;
                ++removed;
            }
            otherStores.erase(store->disp);
            stores[store->disp] = i;
            continue;
        }

        if (auto load = frameLoad(instrs[i])) {
            lastGprStore.erase(load->disp);
            lastXmmStore.erase(load->disp);
            continue;
        }

        if (isMemoryBarrier(instrs[i])) {
            lastGprStore.clear();
            lastXmmStore.clear();
        }
    }

    if (removed != 0) {
        removeMarkedInstructions(instrs, remove);
        stats.deadCodeEliminated += removed;
    }
    return removed;
}

/// @brief Replace subsequent loads of a frame slot with the stored register.
/// @details For each frame store, scans forward looking for a same-slot load.
///          When found and the source register is still live (not clobbered
///          and no memory barrier was crossed), the load is rewritten to a
///          register-to-register move and removed if it becomes a no-op.
///          Bails out on memory barriers, aliasing later stores, or
///          redefinitions of the stored register.
/// @param instrs Block instructions, mutated in place.
/// @param stats Pass-wide statistics accumulator.
/// @return Number of loads eliminated through forwarding.
std::size_t forwardFrameStoreLoads(std::vector<MInstr> &instrs, PeepholeStats &stats) {
    std::size_t forwarded = 0;

    for (std::size_t i = 0; i < instrs.size(); ++i) {
        auto store = frameStore(instrs[i]);
        if (!store || instrs[i].operands.size() < 2)
            continue;

        const Operand storedReg = instrs[i].operands[1];
        for (std::size_t j = i + 1; j < instrs.size(); ++j) {
            if (isMemoryBarrier(instrs[j]))
                break;

            if (auto laterStore = frameStore(instrs[j]);
                laterStore && sameFrameAddress(*store, *laterStore))
                break;

            if (definesOperandReg(instrs[j], storedReg))
                break;

            auto load = frameLoad(instrs[j]);
            if (!load || !sameFrameSlot(*store, *load))
                continue;
            if (instrs[j].operands.empty())
                continue;

            const MOpcode mov = store->cls == RegClass::XMM ? MOpcode::MOVSDrr : MOpcode::MOVrr;
            instrs[j] = MInstr::make(mov, {instrs[j].operands[0], storedReg});
            ++forwarded;
        }
    }

    stats.deadCodeEliminated += forwarded;
    return forwarded;
}

} // namespace viper::codegen::x64::peephole
