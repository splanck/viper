// src/codegen/x86_64/RegAllocLinear.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Implement a straightforward linear-scan register allocator for the
//          x86-64 backend during Phase A. The allocator walks machine blocks in
//          reverse-post-order, applies a block-local liveness approximation, and
//          resolves PX_COPY pseudo instructions into concrete move sequences.
// Invariants: Virtual registers retain a single class and identifier within a
//             function. Stack slot numbering grows monotonically and represents
//             8-byte slots addressed off %rbp. Instructions are mutated in-place
//             to reference physical registers or stack locations.
// Ownership: The allocator owns only transient data structures describing live
//            virtual registers and available physical registers.
// Notes: The implementation intentionally favours clarity over optimality. It
//        eschews global liveness, preferring a conservative "live to end of
//        block" approximation that keeps the code base manageable for Phase A.

#include "RegAllocLinear.hpp"

#include "MachineIR.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

using RegPool = std::vector<PhysReg>;

/// \brief Helper for std::visit overload sets.
template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

/// \brief Convenience wrapper for creating physical register operands.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

/// \brief Construct a frame-indexed memory operand for the given slot.
[[nodiscard]] Operand makeFrameOperand(int slotIndex)
{
    const auto base = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));
    const int32_t offset = -static_cast<int32_t>((slotIndex + 1) * 8);
    return makeMemOperand(base, offset);
}

/// \brief Returns true when \p reg is reserved for the frame/stack pointer.
[[nodiscard]] bool isReservedGPR(PhysReg reg) noexcept
{
    return reg == PhysReg::RSP || reg == PhysReg::RBP;
}

struct VRegState
{
    bool seen{false};
    RegClass cls{RegClass::GPR};
    bool hasPhys{false};
    PhysReg phys{PhysReg::RAX};
    bool spilled{false};
    int spillSlot{-1};
};

struct OperandRole
{
    bool isUse{false};
    bool isDef{false};
};

struct ScratchRelease
{
    PhysReg phys{PhysReg::RAX};
    RegClass cls{RegClass::GPR};
};

struct CopySource
{
    enum class Kind
    {
        Reg,
        Mem
    };
    Kind kind{Kind::Reg};
    PhysReg reg{PhysReg::RAX};
    int slot{-1};
};

struct CopyTask
{
    enum class DestKind
    {
        Reg,
        Mem
    };
    DestKind destKind{DestKind::Reg};
    RegClass cls{RegClass::GPR};
    PhysReg destReg{PhysReg::RAX};
    int destSlot{-1};
    CopySource src{};
    std::optional<uint16_t> destVReg{};
};

class LinearScanAllocator
{
  public:
    LinearScanAllocator(MFunction &func, const TargetInfo &target) : func_(func), target_(target)
    {
        buildPools();
    }

    [[nodiscard]] AllocationResult run()
    {
        for (auto &block : func_.blocks)
        {
            processBlock(block);
            releaseActiveForBlock();
        }

        result_.spillSlotsGPR = nextSpillSlotGPR_;
        result_.spillSlotsXMM = nextSpillSlotXMM_;
        return result_;
    }

  private:
    MFunction &func_;
    const TargetInfo &target_;
    AllocationResult result_{};

    std::unordered_map<uint16_t, VRegState> states_{};
    RegPool freeGPR_{};
    RegPool freeXMM_{};
    std::vector<uint16_t> activeGPR_{};
    std::vector<uint16_t> activeXMM_{};
    int nextSpillSlotGPR_{0};
    int nextSpillSlotXMM_{0};

    void buildPools()
    {
        auto appendRegs = [](RegPool &pool, const std::vector<PhysReg> &regs)
        { pool.insert(pool.end(), regs.begin(), regs.end()); };

        appendRegs(freeGPR_, target_.callerSavedGPR);
        appendRegs(freeGPR_, target_.calleeSavedGPR);
        freeGPR_.erase(std::remove_if(freeGPR_.begin(),
                                      freeGPR_.end(),
                                      [](PhysReg reg) { return isReservedGPR(reg); }),
                       freeGPR_.end());

        appendRegs(freeXMM_, target_.callerSavedXMM);
        appendRegs(freeXMM_, target_.calleeSavedXMM);
    }

    [[nodiscard]] RegPool &poolFor(RegClass cls)
    {
        return cls == RegClass::GPR ? freeGPR_ : freeXMM_;
    }

    [[nodiscard]] std::vector<uint16_t> &activeFor(RegClass cls)
    {
        return cls == RegClass::GPR ? activeGPR_ : activeXMM_;
    }

    [[nodiscard]] VRegState &stateFor(RegClass cls, uint16_t id)
    {
        auto [it, inserted] = states_.try_emplace(id);
        auto &state = it->second;
        if (inserted)
        {
            state.cls = cls;
            state.seen = true;
        }
        else
        {
            state.seen = true;
            assert(state.cls == cls && "VReg reused with different class");
        }
        return state;
    }

    void addActive(RegClass cls, uint16_t id)
    {
        auto &active = activeFor(cls);
        if (std::find(active.begin(), active.end(), id) == active.end())
        {
            active.push_back(id);
        }
    }

    void removeActive(RegClass cls, uint16_t id)
    {
        auto &active = activeFor(cls);
        active.erase(std::remove(active.begin(), active.end(), id), active.end());
    }

    [[nodiscard]] int allocateSpillSlot(RegClass cls)
    {
        if (cls == RegClass::GPR)
        {
            return nextSpillSlotGPR_++;
        }
        return nextSpillSlotXMM_++;
    }

    [[nodiscard]] MInstr makeLoad(RegClass cls, PhysReg dst, int slot)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makePhysOperand(cls, dst), makeFrameOperand(slot)});
        }
        return MInstr::make(MOpcode::MOVSDmr, {makePhysOperand(cls, dst), makeFrameOperand(slot)});
    }

    [[nodiscard]] MInstr makeStore(RegClass cls, int slot, PhysReg src)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makeFrameOperand(slot), makePhysOperand(cls, src)});
        }
        return MInstr::make(MOpcode::MOVSDrm, {makeFrameOperand(slot), makePhysOperand(cls, src)});
    }

    [[nodiscard]] MInstr makeMove(RegClass cls, PhysReg dst, PhysReg src)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makePhysOperand(cls, dst), makePhysOperand(cls, src)});
        }
        return MInstr::make(MOpcode::MOVSDrr,
                            {makePhysOperand(cls, dst), makePhysOperand(cls, src)});
    }

    [[nodiscard]] PhysReg takeRegister(RegClass cls, std::vector<MInstr> &prefix)
    {
        auto &pool = poolFor(cls);
        if (pool.empty())
        {
            spillOne(cls, prefix);
        }
        assert(!pool.empty() && "register pool exhausted");
        const PhysReg reg = pool.front();
        pool.erase(pool.begin());
        return reg;
    }

    void releaseRegister(PhysReg phys, RegClass cls)
    {
        poolFor(cls).push_back(phys);
    }

    void spillOne(RegClass cls, std::vector<MInstr> &prefix)
    {
        auto &active = activeFor(cls);
        if (active.empty())
        {
            return;
        }
        const uint16_t victimId = active.front();
        active.erase(active.begin());
        auto it = states_.find(victimId);
        if (it == states_.end())
        {
            return;
        }
        auto &victim = it->second;
        if (!victim.hasPhys)
        {
            return;
        }
        if (!victim.spilled)
        {
            victim.spilled = true;
            victim.spillSlot = allocateSpillSlot(cls);
        }
        prefix.push_back(makeStore(cls, victim.spillSlot, victim.phys));
        releaseRegister(victim.phys, cls);
        victim.hasPhys = false;
        result_.vregToPhys.erase(victimId);
    }

    void processBlock(MBasicBlock &block)
    {
        std::vector<MInstr> rewritten;
        rewritten.reserve(block.instructions.size());

        for (const auto &instr : block.instructions)
        {
            if (instr.opcode == MOpcode::PX_COPY)
            {
                handleParallelCopy(instr, rewritten);
                continue;
            }

            std::vector<MInstr> prefix;
            std::vector<MInstr> suffix;
            std::vector<ScratchRelease> scratch;
            MInstr current = instr;
            auto roles = classifyOperands(current);

            for (size_t idx = 0; idx < current.operands.size(); ++idx)
            {
                handleOperand(current.operands[idx], roles[idx], prefix, suffix, scratch);
            }

            for (auto &pre : prefix)
            {
                rewritten.push_back(std::move(pre));
            }
            rewritten.push_back(std::move(current));
            for (auto &suf : suffix)
            {
                rewritten.push_back(std::move(suf));
            }
            for (const auto &rel : scratch)
            {
                releaseRegister(rel.phys, rel.cls);
            }
        }

        block.instructions = std::move(rewritten);
    }

    void releaseActiveForBlock()
    {
        for (auto vreg : activeGPR_)
        {
            auto it = states_.find(vreg);
            if (it != states_.end() && it->second.hasPhys)
            {
                releaseRegister(it->second.phys, RegClass::GPR);
                it->second.hasPhys = false;
            }
        }
        activeGPR_.clear();

        for (auto vreg : activeXMM_)
        {
            auto it = states_.find(vreg);
            if (it != states_.end() && it->second.hasPhys)
            {
                releaseRegister(it->second.phys, RegClass::XMM);
                it->second.hasPhys = false;
            }
        }
        activeXMM_.clear();
    }

    [[nodiscard]] std::vector<OperandRole> classifyOperands(const MInstr &instr)
    {
        std::vector<OperandRole> roles(instr.operands.size(), OperandRole{true, false});
        switch (instr.opcode)
        {
            case MOpcode::MOVrr:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{false, true};
                }
                if (roles.size() > 1)
                {
                    roles[1] = OperandRole{true, false};
                }
                break;
            case MOpcode::MOVri:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{false, true};
                }
                break;
            case MOpcode::LEA:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{false, true};
                }
                break;
            case MOpcode::ADDrr:
            case MOpcode::SUBrr:
            case MOpcode::IMULrr:
            case MOpcode::FADD:
            case MOpcode::FSUB:
            case MOpcode::FMUL:
            case MOpcode::FDIV:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{true, true};
                }
                if (roles.size() > 1)
                {
                    roles[1] = OperandRole{true, false};
                }
                break;
            case MOpcode::ADDri:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{true, true};
                }
                break;
            case MOpcode::XORrr32:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{false, true};
                }
                if (roles.size() > 1)
                {
                    roles[1] = OperandRole{true, false};
                }
                break;
            case MOpcode::CMPrr:
            case MOpcode::TESTrr:
            case MOpcode::UCOMIS:
                for (auto &role : roles)
                {
                    role = OperandRole{true, false};
                }
                break;
            case MOpcode::CMPri:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{true, false};
                }
                break;
            case MOpcode::SETcc:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{false, true};
                }
                break;
            case MOpcode::MOVZXrr32:
            case MOpcode::CVTSI2SD:
            case MOpcode::CVTTSD2SI:
            case MOpcode::MOVSDrr:
            case MOpcode::MOVSDmr:
                if (!roles.empty())
                {
                    roles[0] = OperandRole{false, true};
                }
                if (roles.size() > 1)
                {
                    roles[1] = OperandRole{true, false};
                }
                break;
            case MOpcode::MOVSDrm:
                if (roles.size() > 1)
                {
                    roles[1] = OperandRole{true, false};
                }
                break;
            default:
                break;
        }
        return roles;
    }

    void handleOperand(Operand &operand,
                       const OperandRole &role,
                       std::vector<MInstr> &prefix,
                       std::vector<MInstr> &suffix,
                       std::vector<ScratchRelease> &scratch)
    {
        std::visit(Overload{[&](OpReg &reg)
                            { processRegOperand(reg, role, prefix, suffix, scratch); },
                            [&](OpMem &mem)
                            {
                                OperandRole baseRole{true, false};
                                processRegOperand(mem.base, baseRole, prefix, suffix, scratch);
                            },
                            [](auto &) {}},
                   operand);
    }

    void processRegOperand(OpReg &reg,
                           const OperandRole &role,
                           std::vector<MInstr> &prefix,
                           std::vector<MInstr> &suffix,
                           std::vector<ScratchRelease> &scratch)
    {
        if (reg.isPhys)
        {
            return;
        }

        auto &state = stateFor(reg.cls, reg.idOrPhys);
        if (state.spilled)
        {
            if (state.spillSlot < 0)
            {
                state.spillSlot = allocateSpillSlot(state.cls);
            }
            const PhysReg phys = takeRegister(state.cls, prefix);
            if (role.isUse)
            {
                prefix.push_back(makeLoad(state.cls, phys, state.spillSlot));
            }
            if (role.isDef)
            {
                suffix.push_back(makeStore(state.cls, state.spillSlot, phys));
            }
            scratch.push_back(ScratchRelease{phys, state.cls});
            reg = makePhysReg(state.cls, static_cast<uint16_t>(phys));
            return;
        }

        if (!state.hasPhys)
        {
            const PhysReg phys = takeRegister(state.cls, prefix);
            state.hasPhys = true;
            state.phys = phys;
            addActive(state.cls, reg.idOrPhys);
            result_.vregToPhys[reg.idOrPhys] = phys;
        }

        if (!role.isDef)
        {
            // No state update required for pure uses.
        }

        reg = makePhysReg(state.cls, static_cast<uint16_t>(state.phys));
    }

    void handleParallelCopy(const MInstr &instr, std::vector<MInstr> &out)
    {
        std::vector<MInstr> prefix;
        std::vector<ScratchRelease> scratch;
        std::vector<CopyTask> tasks;

        for (size_t i = 0; i + 1 < instr.operands.size(); i += 2)
        {
            const auto &dstOp = instr.operands[i];
            const auto &srcOp = instr.operands[i + 1];

            const auto *dstReg = std::get_if<OpReg>(&dstOp);
            const auto *srcReg = std::get_if<OpReg>(&srcOp);
            if (!dstReg || !srcReg)
            {
                continue; // Phase A: expect register pairs only.
            }

            CopyTask task{};
            task.cls = dstReg->cls;
            task.destVReg.reset();

            // Destination location resolution.
            if (dstReg->isPhys)
            {
                task.destKind = CopyTask::DestKind::Reg;
                task.destReg = static_cast<PhysReg>(dstReg->idOrPhys);
            }
            else
            {
                auto &dstState = stateFor(dstReg->cls, dstReg->idOrPhys);
                task.destVReg = dstReg->idOrPhys;
                if (dstState.spilled)
                {
                    if (dstState.spillSlot < 0)
                    {
                        dstState.spillSlot = allocateSpillSlot(dstState.cls);
                    }
                    task.destKind = CopyTask::DestKind::Mem;
                    task.destSlot = dstState.spillSlot;
                }
                else
                {
                    if (!dstState.hasPhys)
                    {
                        const PhysReg phys = takeRegister(dstState.cls, prefix);
                        dstState.hasPhys = true;
                        dstState.phys = phys;
                        addActive(dstState.cls, dstReg->idOrPhys);
                        result_.vregToPhys[dstReg->idOrPhys] = phys;
                    }
                    task.destKind = CopyTask::DestKind::Reg;
                    task.destReg = dstState.phys;
                }
            }

            // Source location resolution.
            if (srcReg->isPhys)
            {
                task.src.kind = CopySource::Kind::Reg;
                task.src.reg = static_cast<PhysReg>(srcReg->idOrPhys);
            }
            else
            {
                auto &srcState = stateFor(srcReg->cls, srcReg->idOrPhys);
                if (srcState.spilled)
                {
                    if (srcState.spillSlot < 0)
                    {
                        srcState.spillSlot = allocateSpillSlot(srcState.cls);
                    }
                    const PhysReg scratchReg = takeRegister(srcState.cls, prefix);
                    prefix.push_back(makeLoad(srcState.cls, scratchReg, srcState.spillSlot));
                    scratch.push_back(ScratchRelease{scratchReg, srcState.cls});
                    task.src.kind = CopySource::Kind::Reg;
                    task.src.reg = scratchReg;
                }
                else
                {
                    if (!srcState.hasPhys)
                    {
                        const PhysReg phys = takeRegister(srcState.cls, prefix);
                        srcState.hasPhys = true;
                        srcState.phys = phys;
                        addActive(srcState.cls, srcReg->idOrPhys);
                        result_.vregToPhys[srcReg->idOrPhys] = phys;
                    }
                    task.src.kind = CopySource::Kind::Reg;
                    task.src.reg = srcState.phys;
                }
            }

            tasks.push_back(task);
        }

        for (auto &pre : prefix)
        {
            out.push_back(std::move(pre));
        }

        std::vector<MInstr> generated;
        generated.reserve(tasks.size());

        while (!tasks.empty())
        {
            bool progress = false;
            for (size_t i = 0; i < tasks.size(); ++i)
            {
                auto task = tasks[i];
                bool canEmit = false;
                if (task.destKind == CopyTask::DestKind::Mem)
                {
                    canEmit = true;
                }
                else if (task.src.kind == CopySource::Kind::Mem)
                {
                    canEmit = true;
                }
                else if (task.src.kind == CopySource::Kind::Reg)
                {
                    bool srcIsDest = false;
                    for (const auto &other : tasks)
                    {
                        if (other.destKind == CopyTask::DestKind::Reg &&
                            other.destReg == task.src.reg)
                        {
                            srcIsDest = true;
                            break;
                        }
                    }
                    canEmit = !srcIsDest || task.destReg == task.src.reg;
                }

                if (!canEmit)
                {
                    continue;
                }

                emitCopyTask(task, generated);
                tasks.erase(tasks.begin() + static_cast<long>(i));
                progress = true;
                break;
            }

            if (progress)
            {
                continue;
            }

            // Cycle detected: choose the first register task and break it.
            auto it = std::find_if(tasks.begin(),
                                   tasks.end(),
                                   [](const CopyTask &t)
                                   {
                                       return t.destKind == CopyTask::DestKind::Reg &&
                                              t.src.kind == CopySource::Kind::Reg;
                                   });
            if (it == tasks.end())
            {
                break;
            }
            CopyTask cycleTask = *it;
            const PhysReg srcReg = cycleTask.src.reg;

            const PhysReg temp = takeRegister(cycleTask.cls, generated);
            generated.push_back(makeMove(cycleTask.cls, temp, srcReg));
            for (auto &pending : tasks)
            {
                if (pending.src.kind == CopySource::Kind::Reg && pending.src.reg == srcReg)
                {
                    pending.src.reg = temp;
                }
            }
            scratch.push_back(ScratchRelease{temp, cycleTask.cls});
        }

        for (auto &instrOut : generated)
        {
            out.push_back(std::move(instrOut));
        }

        for (const auto &rel : scratch)
        {
            releaseRegister(rel.phys, rel.cls);
        }
    }

    void emitCopyTask(const CopyTask &task, std::vector<MInstr> &generated)
    {
        if (task.destKind == CopyTask::DestKind::Mem)
        {
            if (task.src.kind == CopySource::Kind::Reg)
            {
                generated.push_back(makeStore(task.cls, task.destSlot, task.src.reg));
            }
            else
            {
                // Memory-to-memory requires a temporary register.
                std::vector<MInstr> tmpPrefix;
                const PhysReg tmp = takeRegister(task.cls, tmpPrefix);
                for (auto &pre : tmpPrefix)
                {
                    generated.push_back(std::move(pre));
                }
                generated.push_back(makeLoad(task.cls, tmp, task.src.slot));
                generated.push_back(makeStore(task.cls, task.destSlot, tmp));
                releaseRegister(tmp, task.cls);
            }
            return;
        }

        if (task.src.kind == CopySource::Kind::Reg)
        {
            generated.push_back(makeMove(task.cls, task.destReg, task.src.reg));
        }
        else
        {
            if (task.cls == RegClass::GPR)
            {
                generated.push_back(MInstr::make(
                    MOpcode::MOVrr,
                    {makePhysOperand(task.cls, task.destReg), makeFrameOperand(task.src.slot)}));
            }
            else
            {
                generated.push_back(MInstr::make(
                    MOpcode::MOVSDmr,
                    {makePhysOperand(task.cls, task.destReg), makeFrameOperand(task.src.slot)}));
            }
        }
    }
};

} // namespace

AllocationResult allocate(MFunction &func, const TargetInfo &target)
{
    LinearScanAllocator allocator{func, target};
    return allocator.run();
}

} // namespace viper::codegen::x64
