//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/RegAllocLinear.cpp
// Purpose: Provide the Phase-A linear-scan register allocator for the x86-64
//          backend.
// Key invariants:
//   * Each Machine IR virtual register is associated with exactly one register
//     class and, when active, one physical register.
//   * Spill slots are 8-byte stack locations addressed relative to %rbp and are
//     allocated monotonically.
//   * PX_COPY pseudo instructions are expanded into explicit move sequences
//     before the function leaves the allocator.
// Ownership model: The allocator mutates Machine IR blocks in place while
// maintaining transient state describing active virtual registers, scratch
// temporaries, and spill slots.  All state is released once allocation
// completes.
// Links: docs/architecture.md#codegen
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements a readability-first linear-scan allocator for Machine IR.
/// @details The allocator walks Machine IR blocks in reverse-post-order,
///          assigning physical registers from target-specific pools.  When pools
///          empty, registers are spilled into stack slots and reloaded on demand
///          using helper routines that preserve register class semantics.  The
///          implementation is intentionally conservative, approximating liveness
///          on a per-block basis to keep Phase-A development approachable.

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

/// @brief Build a Machine IR operand referencing a concrete physical register.
/// @details Helper used throughout the allocator when inserting moves or loads
///          that target a specific architectural register.  The function wraps
///          the register class and numeric identifier into the variant used by
///          Machine IR instructions.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

/// @brief Form a memory operand that references a stack slot relative to %rbp.
/// @details Spill slots are addressed as negative offsets from the frame
///          pointer.  This helper converts the slot index into the correct
///          displacement and packages it into a Machine IR memory operand with
///          %rbp as the base register.
[[nodiscard]] Operand makeFrameOperand(int slotIndex)
{
    const auto base = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));
    const int32_t offset = -static_cast<int32_t>((slotIndex + 1) * 8);
    return makeMemOperand(base, offset);
}

/// @brief Check whether a general-purpose register is reserved for ABI use.
/// @details The allocator never hands out %rsp or %rbp because they anchor the
///          stack frame.  Reserving them here simplifies register-pool
///          construction.
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
    /// @brief Construct an allocator bound to a Machine IR function.
    /// @details The constructor stores references to the function under
    ///          mutation and the target description before eagerly constructing
    ///          the register pools.  Building the pools up front keeps the
    ///          hot path free from repeated target queries.
    LinearScanAllocator(MFunction &func, const TargetInfo &target) : func_(func), target_(target)
    {
        buildPools();
    }

    /// @brief Run the allocation pass over every block in reverse-post-order.
    /// @details Walks the function, rewriting each instruction so virtual
    ///          register operands refer to physical registers or stack slots.
    ///          After all blocks have been processed the method records spill
    ///          usage statistics and returns them to the caller.
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

    /// @brief Populate register pools based on the target's available registers.
    /// @details The allocator concatenates caller- and callee-saved sets for each
    ///          register class before removing reserved registers.  The pools are
    ///          later treated as LIFO stacks when allocating scratch registers.
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

    /// @brief Select the register pool that corresponds to a register class.
    /// @details Returns either the general-purpose pool or the floating-point
    ///          pool depending on @p cls.  The pools are mutated in place when
    ///          registers are allocated or released.
    [[nodiscard]] RegPool &poolFor(RegClass cls)
    {
        return cls == RegClass::GPR ? freeGPR_ : freeXMM_;
    }

    /// @brief Retrieve the active-virtual-register list for a class.
    /// @details Active sets track which virtual registers currently hold
    ///          physical registers.  This data drives spill decisions when pools
    ///          run empty.
    [[nodiscard]] std::vector<uint16_t> &activeFor(RegClass cls)
    {
        return cls == RegClass::GPR ? activeGPR_ : activeXMM_;
    }

    /// @brief Fetch (or create) bookkeeping for a virtual register.
    /// @details The allocator lazily initialises @ref VRegState entries so it can
    ///          record register class, spill status, and current physical
    ///          assignment.  Subsequent lookups reuse the cached state and assert
    ///          that the register class remains stable.
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

    /// @brief Record that a virtual register currently holds a physical register.
    /// @details Active sets guard against double-insertion and later feed spill
    ///          heuristics.  Only registers that are live past the current
    ///          instruction are tracked.
    void addActive(RegClass cls, uint16_t id)
    {
        auto &active = activeFor(cls);
        if (std::find(active.begin(), active.end(), id) == active.end())
        {
            active.push_back(id);
        }
    }

    /// @brief Drop a virtual register from the active set.
    /// @details Called once a register is known to no longer require a physical
    ///          register for the remainder of the block, making it eligible for
    ///          reuse or spill without violating liveness assumptions.
    void removeActive(RegClass cls, uint16_t id)
    {
        auto &active = activeFor(cls);
        active.erase(std::remove(active.begin(), active.end(), id), active.end());
    }

    /// @brief Reserve a new stack slot for the given register class.
    /// @details GPR and XMM spills are tracked independently so stack layout can
    ///          later respect class-specific alignment requirements.  Slots are
    ///          numbered densely from zero.
    [[nodiscard]] int allocateSpillSlot(RegClass cls)
    {
        if (cls == RegClass::GPR)
        {
            return nextSpillSlotGPR_++;
        }
        return nextSpillSlotXMM_++;
    }

    /// @brief Build a load instruction that restores a spilled value.
    /// @details Encapsulates the class-specific opcode selection required to
    ///          load from a frame slot back into a physical register.
    [[nodiscard]] MInstr makeLoad(RegClass cls, PhysReg dst, int slot)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makePhysOperand(cls, dst), makeFrameOperand(slot)});
        }
        return MInstr::make(MOpcode::MOVSDmr, {makePhysOperand(cls, dst), makeFrameOperand(slot)});
    }

    /// @brief Build a store instruction that spills a value to the stack.
    /// @details Mirrors @ref makeLoad by emitting the appropriate move opcode
    ///          for the register class while referencing the requested frame
    ///          slot.
    [[nodiscard]] MInstr makeStore(RegClass cls, int slot, PhysReg src)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makeFrameOperand(slot), makePhysOperand(cls, src)});
        }
        return MInstr::make(MOpcode::MOVSDrm, {makeFrameOperand(slot), makePhysOperand(cls, src)});
    }

    /// @brief Construct a register-to-register move for a register class.
    /// @details Used while resolving PX_COPY cycles or shuffling temporaries.
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

    /// @brief Acquire a physical register from the appropriate pool.
    /// @details When the pool is empty the allocator spills an active register to
    ///          free space before returning a register.  Optional @p prefix
    ///          instructions receive the spill sequence so callers can insert it
    ///          ahead of the instruction being rewritten.
    [[nodiscard]] PhysReg takeRegister(RegClass cls, std::vector<MInstr> &prefix)
    {
        auto &pool = poolFor(cls);
        if (pool.empty())
        {
            spillOne(cls, prefix);
        }
        assert(!pool.empty() && "register pool exhausted");
        const PhysReg reg = pool.back();
        pool.pop_back();
        return reg;
    }

    /// @brief Return a physical register to the allocator's free pool.
    /// @details Called when temporaries expire or when parallel copies finish
    ///          using scratch registers.
    void releaseRegister(PhysReg phys, RegClass cls)
    {
        poolFor(cls).push_back(phys);
    }

    /// @brief Evict one active virtual register to free a physical register.
    /// @details Implements a FIFO heuristic by selecting the first active
    ///          virtual register of the requested class.  The victim's value is
    ///          stored to its spill slot before the physical register is released
    ///          back to the pool.
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

    /// @brief Rewrite every instruction in a Machine IR block.
    /// @details Handles PX_COPY pseudo instructions separately, otherwise walks
    ///          each instruction to classify operands, allocate registers, and
    ///          insert necessary spill/reload sequences.  The rewritten block is
    ///          stored back into the Machine IR function.
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

    /// @brief Release all active physical registers at block boundaries.
    /// @details Linear-scan allocation approximates liveness per block, so any
    ///          registers left active after processing a block are returned to
    ///          the free pools before visiting the next block.
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

    /// @brief Describe whether each operand is used, defined, or both.
    /// @details The classification drives whether the allocator must load from
    ///          a spill slot before using the operand and/or store back after
    ///          defining it.  The logic mirrors the semantics of each opcode the
    ///          allocator currently understands.
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

    /// @brief Rewrite an operand in-place to reference allocated resources.
    /// @details Dispatches based on operand kind, rewriting register operands and
    ///          recursively processing the base register of memory operands so
    ///          addressing modes remain valid after allocation.
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

    /// @brief Allocate or reload the physical register backing a virtual one.
    /// @details Depending on the operand role the helper loads from spill slots,
    ///          marks values for later stores, and records temporary registers so
    ///          they can be released after the instruction executes.
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

    /// @brief Expand a PX_COPY pseudo instruction into explicit moves.
    /// @details Resolves both source and destination locations (registers or
    ///          spill slots), acquires scratch registers when necessary, and
    ///          breaks cycles using temporary registers so the resulting sequence
    ///          faithfully copies all values.
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

    /// @brief Emit the concrete instructions for a single copy task.
    /// @details Handles register-to-register, memory-to-register, and
    ///          register-to-memory combinations, using temporary registers when
    ///          a memory-to-memory transfer is required.
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

/// @brief Entry point that allocates registers for an entire Machine IR function.
/// @details Convenience wrapper that constructs the allocator, runs it, and
///          returns the allocation summary to callers.
AllocationResult allocate(MFunction &func, const TargetInfo &target)
{
    LinearScanAllocator allocator{func, target};
    return allocator.run();
}

} // namespace viper::codegen::x64
