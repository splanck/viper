//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/MemoryOpt.cpp
// Purpose: x86-64 memory peephole optimizations.
//
//===----------------------------------------------------------------------===//

#include "MemoryOpt.hpp"

#include "codegen/x86_64/OperandRoles.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>

namespace viper::codegen::x64::peephole {
namespace {

struct FrameAccess {
    int32_t disp{0};
    RegClass cls{RegClass::GPR};
};

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

bool sameFrameSlot(const FrameAccess &a, const FrameAccess &b) {
    return a.disp == b.disp && a.cls == b.cls;
}

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
            stores[store->disp] = i;
            continue;
        }

        if (auto load = frameLoad(instrs[i])) {
            mapFor(load->cls).erase(load->disp);
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

            if (auto laterStore = frameStore(instrs[j]); laterStore && sameFrameSlot(*store, *laterStore))
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
