//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/codegen/x86_64/RegAllocLinear.cpp
// Purpose: Implement the Phase A linear-scan register allocator for the
//          x86-64 backend, covering virtual-to-physical mapping and spill code
//          synthesis.
// Key invariants: Virtual registers preserve their RegClass for the lifetime of
//                 the Machine IR function, stack slots are addressed relative to
//                 %rbp using 8-byte increments, and PX_COPY pseudos are fully
//                 resolved before the pass exits.
// Ownership/Lifetime: The allocator mutates Machine IR in place and owns only
//                     transient analysis state such as free lists and spill
//                     metadata scoped to a single function run.
// Perf/Threading notes: Designed for clarity rather than optimality; operates on
//                       one function at a time with no shared mutable state and
//                       assumes invocation from a single-threaded pipeline.
// Links: docs/architecture.md#cpp-overview, docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "RegAllocLinear.hpp"

#include "MachineIR.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

using RegPool = std::vector<PhysReg>;

/// @brief Helper template that folds a pack of callables into a single overload set.
/// @details Enables concise visitation of @c std::variant operands when the allocator
///          needs to dispatch based on operand kind.  The pattern mirrors the
///          canonical C++17 "overload" helper seen in the standard library examples
///          and keeps visitor lambdas readable at the call site.
template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

/// @brief Convert a physical register enumerator into a Machine IR operand.
/// @details Delegates to @ref makePhysRegOperand while preserving the explicit
///          register class.  Centralising the helper keeps operand construction
///          consistent across the allocator and documents that the function solely
///          wraps the strongly typed enumeration.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

/// @brief Build a memory operand addressing a spill slot relative to %rbp.
/// @details The allocator treats every spill slot as an 8-byte cell addressed off
///          the frame pointer.  This helper materialises the corresponding
///          @ref OpMem operand by pairing the %rbp base with a negative offset that
///          grows by 8 bytes per slot index.
[[nodiscard]] Operand makeFrameOperand(int slotIndex)
{
    const auto base = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));
    const int32_t offset = -static_cast<int32_t>((slotIndex + 1) * 8);
    return makeMemOperand(base, offset);
}

/// @brief Check whether a physical register is reserved for stack management.
/// @details The linear-scan allocator never hands out %rsp or %rbp because they are
///          required for stack addressing.  The helper keeps the reservation logic in
///          one place so pool construction and validation reference the same rule.
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

/// @brief Implements the linear-scan allocation algorithm for a single function.
/// @details Owns temporary maps that associate virtual registers with physical
///          registers or spill slots while walking Machine IR in block order.  The
///          allocator follows a conservative liveness model—values live until the
///          end of their block—to keep Phase A implementation simple.  All state is
///          discarded once @ref run completes.
class LinearScanAllocator
{
  public:
    /// @brief Create an allocator instance for @p func using @p target conventions.
    /// @details Stores references to the Machine IR function and target info, then
    ///          primes the free-register pools so that @ref run can immediately
    ///          service allocation requests.  Pool construction filters out reserved
    ///          registers such as %rsp/%rbp.
    LinearScanAllocator(MFunction &func, const TargetInfo &target) : func_(func), target_(target)
    {
        buildPools();
    }

    /// @brief Execute linear-scan allocation across all blocks in program order.
    /// @details Iterates each block, rewriting instructions in place while
    ///          synthesising loads, stores, and moves.  After processing a block it
    ///          releases any active physical registers so the next block starts with
    ///          a clean free list.  The final allocation summary is returned to the
    ///          caller.
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

    /// @brief Seed the free-register pools using the target description.
    /// @details Appends caller- and callee-saved registers to the appropriate pools
    ///          and erases the reserved stack pointer pair.  The resulting vectors
    ///          define the order in which registers are handed out during
    ///          allocation.
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

    /// @brief Select the free-register pool matching a register class.
    /// @details Provides a unified lookup for both GPR and XMM pools so helpers can
    ///          operate generically over register classes without branching at the
    ///          call site.
    [[nodiscard]] RegPool &poolFor(RegClass cls)
    {
        return cls == RegClass::GPR ? freeGPR_ : freeXMM_;
    }

    /// @brief Access the vector tracking active virtual registers for a class.
    /// @details The allocator maintains per-class active sets so that spilling and
    ///          release operations can quickly scan the live values.  Returning a
    ///          reference avoids repeated branching elsewhere in the implementation.
    [[nodiscard]] std::vector<uint16_t> &activeFor(RegClass cls)
    {
        return cls == RegClass::GPR ? activeGPR_ : activeXMM_;
    }

    /// @brief Fetch or create the tracking record for a virtual register.
    /// @details Ensures that the stored @ref RegClass matches the caller's view and
    ///          marks the value as seen so later passes know it participates in the
    ///          function.  A new entry initialises with default "unallocated"
    ///          metadata.
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

    /// @brief Register a virtual register as currently mapped to a physical one.
    /// @details Maintains unique membership in the per-class active list so that
    ///          spill heuristics can scan the head of the list without duplicates.
    void addActive(RegClass cls, uint16_t id)
    {
        auto &active = activeFor(cls);
        if (std::find(active.begin(), active.end(), id) == active.end())
        {
            active.push_back(id);
        }
    }

    /// @brief Remove a virtual register from the active set when it leaves scope.
    /// @details Used when instructions release registers or when a value transitions
    ///          to a spilled state.  The function performs a stable erase so other
    ///          entries retain their ordering.
    void removeActive(RegClass cls, uint16_t id)
    {
        auto &active = activeFor(cls);
        active.erase(std::remove(active.begin(), active.end(), id), active.end());
    }

    /// @brief Reserve the next spill slot for a register class.
    /// @details Tracks monotonically increasing indices for both GPR and XMM spill
    ///          areas.  The caller converts the returned index into a frame
    ///          displacement using @ref makeFrameOperand.
    [[nodiscard]] int allocateSpillSlot(RegClass cls)
    {
        if (cls == RegClass::GPR)
        {
            return nextSpillSlotGPR_++;
        }
        return nextSpillSlotXMM_++;
    }

    /// @brief Create a load instruction that rehydrates a spilled value.
    /// @details Selects either the integer or floating-point load opcode and emits
    ///          an operand pair targeting the spill slot identified by @p slot.
    [[nodiscard]] MInstr makeLoad(RegClass cls, PhysReg dst, int slot)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makePhysOperand(cls, dst), makeFrameOperand(slot)});
        }
        return MInstr::make(MOpcode::MOVSDmr, {makePhysOperand(cls, dst), makeFrameOperand(slot)});
    }

    /// @brief Create a store instruction that writes a register into a spill slot.
    /// @details Mirrors @ref makeLoad by choosing the opcode and operand order that
    ///          matches the Machine IR conventions for memory writes.
    [[nodiscard]] MInstr makeStore(RegClass cls, int slot, PhysReg src)
    {
        if (cls == RegClass::GPR)
        {
            return MInstr::make(MOpcode::MOVrr,
                                {makeFrameOperand(slot), makePhysOperand(cls, src)});
        }
        return MInstr::make(MOpcode::MOVSDrm, {makeFrameOperand(slot), makePhysOperand(cls, src)});
    }

    /// @brief Construct a register-to-register move for a given class.
    /// @details Used heavily when materialising PX_COPY expansion sequences.  The
    ///          helper centralises opcode selection for clarity.
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

    /// @brief Acquire a physical register, spilling if the pool is empty.
    /// @details When no registers remain, the allocator forces a spill of the oldest
    ///          active value using @ref spillOne and extends the @p prefix list with
    ///          the generated spill code so the caller can insert it before the
    ///          current instruction.
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

    /// @brief Return a physical register to the free pool after use.
    /// @details Appends the register to the end of the pool so future allocations can
    ///          reuse it.  The loose LRU order produced by this strategy keeps the
    ///          implementation simple while remaining deterministic.
    void releaseRegister(PhysReg phys, RegClass cls)
    {
        poolFor(cls).push_back(phys);
    }

    /// @brief Spill the oldest active virtual register for @p cls.
    /// @details Selects the front of the active list, allocates a spill slot if
    ///          necessary, emits a store into @p prefix, and updates bookkeeping so
    ///          the victim no longer claims a physical register.
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

    /// @brief Rewrite all instructions within a Machine IR block.
    /// @details Iterates instructions, expands PX_COPY pseudos into explicit move
    ///          sequences, and otherwise walks each operand to map virtual registers
    ///          to physical registers or spill loads/stores.  Newly generated
    ///          prologue and epilogue instructions are threaded into @p block in
    ///          their proper order.
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

    /// @brief Release any physical registers that remain live at block end.
    /// @details Clears the per-class active lists and returns their physical
    ///          registers to the free pools so the next block starts from a clean
    ///          slate.  Spill state is retained so values can be reloaded if they
    ///          appear again later.
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

    /// @brief Determine how each operand in @p instr participates in the operation.
    /// @details Returns a vector paralleling the operand list that records whether
    ///          each operand is read, written, or both.  The classification steers
    ///          register allocation decisions such as when to emit reloads or stores.
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

    /// @brief Rewrite a single operand according to its role in the instruction.
    /// @details Visits the operand variant and dispatches to @ref processRegOperand
    ///          for registers.  Memory operands trigger recursive processing of their
    ///          base register so loads and stores honour the same allocation logic.
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

    /// @brief Materialise a virtual register operand into a usable form.
    /// @details Ensures the value has a physical register, loading from a spill slot
    ///          when necessary and queueing stores if the operand defines the value.
    ///          Temporary registers borrowed during the operation are recorded in
    ///          @p scratch so they can be released once the instruction finishes.
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

    /// @brief Expand a PX_COPY pseudo instruction into concrete moves.
    /// @details Breaks multi-operand parallel copies into a sequence of moves that
    ///          honour dependency ordering.  Scratch registers are borrowed to
    ///          resolve cycles, and spill slots are consulted when either the source
    ///          or destination currently resides in memory.
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

    /// @brief Emit concrete instructions for a single copy operation.
    /// @details Handles register-to-register, register-to-memory, and memory-to-
    ///          register transfers, borrowing temporaries when a memory-to-memory
    ///          copy must be decomposed.  Appends the generated instructions to
    ///          @p generated in execution order.
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

/// @brief Entry point that performs linear-scan allocation on a Machine IR function.
/// @details Instantiates @ref LinearScanAllocator with the provided function and
///          target information, then delegates to @ref LinearScanAllocator::run to
///          rewrite the function in place.  The resulting summary describes spill
///          usage and the final virtual-to-physical mappings observed during the
///          pass.
AllocationResult allocate(MFunction &func, const TargetInfo &target)
{
    LinearScanAllocator allocator{func, target};
    return allocator.run();
}

} // namespace viper::codegen::x64
