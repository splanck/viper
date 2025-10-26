//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/RegAllocLinear.cpp
// Purpose: Provide a linear-scan register allocator for the x86-64 backend
//          during Phase A.
// Key invariants: Virtual registers preserve a stable class throughout a
//                 function, spill slots are addressed off %rbp in 8-byte steps,
//                 and PX_COPY bundles are lowered into explicit move sequences
//                 before code emission.
// Ownership/Lifetime: The allocator mutates Machine IR owned by the caller
//                     while using transient data structures to track live
//                     values and scratch registers.
// Links: docs/codemap.md, src/codegen/x86_64/RegAllocLinear.hpp,
//        src/codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements a linear-scan allocator specialised for Viper's Phase-A
///        x86-64 backend.
/// @details The allocator traverses Machine IR blocks, assigns physical
///          registers from ABI-provided pools, materialises spill loads/stores
///          when registers exhaust, and expands PX_COPY instructions into
///          executable sequences while respecting per-class register
///          constraints.

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

/// @brief Helper for `std::visit` overload sets.
template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

/// @brief Convenience wrapper for creating physical register operands.
/// @param cls Register class describing the operand.
/// @param reg Concrete physical register identifier.
/// @return Machine IR operand referencing the register.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

/// @brief Construct a frame-indexed memory operand for the given spill slot.
/// @param slotIndex Zero-based spill slot index.
/// @return Operand addressing the slot relative to %rbp.
[[nodiscard]] Operand makeFrameOperand(int slotIndex)
{
    const auto base = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));
    const int32_t offset = -static_cast<int32_t>((slotIndex + 1) * 8);
    return makeMemOperand(base, offset);
}

/// @brief Returns true when @p reg is reserved for the frame/stack pointer.
/// @param reg Physical register to test.
/// @return True for %rsp or %rbp.
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

/// @brief Linear-scan allocator that walks Machine IR blocks sequentially.
/// @details Owns the transient state required to map virtual registers onto
///          physical registers or spill slots.  The allocator consumes ABI
///          register pools supplied by @ref TargetInfo and records the mapping
///          in @ref AllocationResult for downstream passes.
class LinearScanAllocator
{
  public:
    /// @brief Construct the allocator and seed the available register pools.
    /// @param func Machine function to allocate.
    /// @param target Target description providing register classes and ABI sets.
    LinearScanAllocator(MFunction &func, const TargetInfo &target) : func_(func), target_(target)
    {
        buildPools();
    }

    /// @brief Run the allocation pass across every block in the function.
    /// @details Processes blocks in source order, assigning registers, emitting
    ///          spill code, and expanding PX_COPY instructions.  After each
    ///          block the active register set is cleared to the conservative
    ///          "live to block end" approximation used by Phase A.
    /// @return Allocation summary including spill slot counts and vreg->phys map.
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

    /// @brief Populate the free-register pools using the target ABI description.
    /// @details Concatenates caller-saved and callee-saved registers and removes
    ///          reserved registers such as %rsp and %rbp from the GPR pool.
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

    /// @brief Select the register pool that corresponds to @p cls.
    /// @param cls Register class (GPR or XMM).
    /// @return Mutable pool of free physical registers for the class.
    [[nodiscard]] RegPool &poolFor(RegClass cls)
    {
        return cls == RegClass::GPR ? freeGPR_ : freeXMM_;
    }

    /// @brief Retrieve the active set vector tracking vregs currently resident.
    /// @param cls Register class being queried.
    /// @return Reference to the per-class active vector.
    [[nodiscard]] std::vector<uint16_t> &activeFor(RegClass cls)
    {
        return cls == RegClass::GPR ? activeGPR_ : activeXMM_;
    }

    /// @brief Look up or initialise the allocation state for @p id.
    /// @details Ensures that the vreg state tracks the register class and marks
    ///          the value as seen so subsequent passes know the vreg exists.
    /// @param cls Register class that owns the vreg.
    /// @param id Virtual register identifier.
    /// @return Mutable state record for the virtual register.
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

    /// @brief Track that @p id currently occupies a physical register.
    /// @param cls Register class that @p id belongs to.
    /// @param id Virtual register identifier.
    void addActive(RegClass cls, uint16_t id)
    {
        auto &active = activeFor(cls);
        if (std::find(active.begin(), active.end(), id) == active.end())
        {
            active.push_back(id);
        }
    }

    /// @brief Remove @p id from the active set once it no longer holds a phys.
    /// @param cls Register class that @p id belongs to.
    /// @param id Virtual register identifier.
    void removeActive(RegClass cls, uint16_t id)
    {
        auto &active = activeFor(cls);
        active.erase(std::remove(active.begin(), active.end(), id), active.end());
    }

    /// @brief Allocate a spill slot for the given register class.
    /// @param cls Register class needing a new spill slot.
    /// @return Zero-based slot index assigned to the spill.
    [[nodiscard]] int allocateSpillSlot(RegClass cls)
    {
        if (cls == RegClass::GPR)
        {
            return nextSpillSlotGPR_++;
        }
        return nextSpillSlotXMM_++;
    }

    /// @brief Build a load instruction that restores a value from a spill slot.
    /// @param cls Register class describing the payload.
    /// @param dst Physical register destination.
    /// @param slot Spill slot index to load from.
    /// @return MIR instruction that performs the load.
    [[nodiscard]] MInstr makeLoad(RegClass cls, PhysReg dst, int slot)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makePhysOperand(cls, dst), makeFrameOperand(slot)});
        }
        return MInstr::make(MOpcode::MOVSDmr, {makePhysOperand(cls, dst), makeFrameOperand(slot)});
    }

    /// @brief Build a store instruction that spills a value into a slot.
    /// @param cls Register class describing the payload.
    /// @param slot Spill slot index to store into.
    /// @param src Physical register source.
    /// @return MIR instruction that performs the store.
    [[nodiscard]] MInstr makeStore(RegClass cls, int slot, PhysReg src)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makeFrameOperand(slot), makePhysOperand(cls, src)});
        }
        return MInstr::make(MOpcode::MOVSDrm, {makeFrameOperand(slot), makePhysOperand(cls, src)});
    }

    /// @brief Emit a register-to-register move for the given class.
    /// @param cls Register class being manipulated.
    /// @param dst Destination physical register.
    /// @param src Source physical register.
    /// @return MIR instruction representing the move.
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

    /// @brief Obtain a physical register, spilling another value if necessary.
    /// @details When the pool is empty the allocator spills one active vreg to
    ///          free space.  Any spill code is appended to @p prefix so it
    ///          executes before the current instruction.
    /// @param cls Register class requested.
    /// @param prefix Instruction list receiving spill code.
    /// @return Newly reserved physical register.
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

    /// @brief Return @p phys to the free pool for @p cls.
    /// @param phys Physical register being released.
    /// @param cls Register class that owns the pool.
    void releaseRegister(PhysReg phys, RegClass cls)
    {
        poolFor(cls).push_back(phys);
    }

    /// @brief Spill one active value for @p cls to make space for a new phys.
    /// @details Selects the first active vreg, allocates a spill slot when
    ///          required, emits the store into @p prefix, and releases the
    ///          associated physical register.
    /// @param cls Register class whose pool is exhausted.
    /// @param prefix Instruction list receiving spill code.
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

    /// @brief Allocate registers for every instruction in @p block.
    /// @details Rewrites each instruction in sequence, inserting loads, stores,
    ///          and moves as needed to materialise operands.  PX_COPY bundles
    ///          are handled separately to maintain atomic semantics.
    /// @param block Machine IR block being processed.
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

    /// @brief Release all active physical registers at the end of a block.
    /// @details Implements the conservative liveness approximation used by the
    ///          prototype allocator: values are considered dead after the block
    ///          boundary, so their registers return to the free pool.
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

    /// @brief Classify operands as uses or defs for scheduling register actions.
    /// @details Returns a vector parallel to the operand list indicating
    ///          whether each operand contributes a read, write, or both.  This
    ///          guides how @ref handleOperand manipulates state.
    /// @param instr Instruction whose operands are being classified.
    /// @return Vector of operand roles.
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

    /// @brief Prepare @p operand for execution by materialising registers.
    /// @details Handles both register and memory operands.  When virtual
    ///          registers appear the helper ensures they own physical registers
    ///          or emits spill loads/stores around the instruction as needed.
    /// @param operand Operand being processed.
    /// @param role Role describing how the operand is used.
    /// @param prefix Instruction list receiving pre-instruction loads/moves.
    /// @param suffix Instruction list receiving post-instruction stores.
    /// @param scratch Registers that should be released after the instruction.
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

    /// @brief Process a virtual register operand and allocate backing storage.
    /// @details Ensures the operand has a physical register, loading from or
    ///          spilling to the associated slot when necessary.  Scratch
    ///          registers used for spilled values are tracked so they can be
    ///          released after the instruction executes.
    /// @param reg Register operand to process.
    /// @param role Descriptor specifying whether the operand is read and/or
    ///             written by the instruction.
    /// @param prefix Instruction list receiving pre-instruction loads/moves.
    /// @param suffix Instruction list receiving post-instruction stores.
    /// @param scratch Registers scheduled for release post-instruction.
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

    /// @brief Lower a PX_COPY bundle into explicit move instructions.
    /// @details Decodes the interleaved destination/source operand pairs,
    ///          resolves each operand to registers or spills, and emits a
    ///          sequence of moves that preserves parallel copy semantics.
    /// @param instr PX_COPY instruction being expanded.
    /// @param out Instruction list receiving the lowered moves.
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

    /// @brief Emit the concrete instruction sequence for a resolved copy task.
    /// @details Depending on the destination (register or memory) and source
    ///          (register or memory) this emits a single move/store or material
    ///          uses of temporary registers to maintain correctness.
    /// @param task Copy description produced by @ref handleParallelCopy.
    /// @param generated Output instruction list receiving the materialised copy.
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

/// @brief Convenience entry point that allocates a machine function in-place.
/// @details Instantiates the linear-scan allocator with the provided machine
///          function and target description, then runs it to obtain the final
///          allocation summary used by later lowering stages.
/// @param func Machine function to allocate.
/// @param target Target ABI description.
/// @return Allocation summary describing spills and physical register mapping.
AllocationResult allocate(MFunction &func, const TargetInfo &target)
{
    LinearScanAllocator allocator{func, target};
    return allocator.run();
}

} // namespace viper::codegen::x64
