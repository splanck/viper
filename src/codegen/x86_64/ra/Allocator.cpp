//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/Allocator.cpp
// Purpose: Implement the linear-scan allocation phase which assigns physical
//          registers, inserts spill code, and lowers PX_COPY bundles for the
//          x86-64 backend.
// Key invariants: Register pools are deterministically populated from the
//                 target ABI, and allocation proceeds in block order releasing
//                 all live values at block boundaries.
// Ownership/Lifetime: Mutates Machine IR blocks in place and returns an
//                     AllocationResult summarising register assignments and
//                     spill slot counts.
// Links: src/codegen/x86_64/ra/Allocator.hpp
//
//===----------------------------------------------------------------------===//

#include "Allocator.hpp"

#include "Coalescer.hpp"

#include <algorithm>
#include <cassert>

/// @file
/// @brief Implements the x86-64 linear-scan register allocator.
/// @details The allocator walks Machine IR blocks in order, leasing physical
///          registers from ABI-configured pools, spilling values when pressure
///          grows, and invoking the coalescer to expand PX_COPY pseudos.  The
///          implementation maintains per-class pools and active lists so live
///          ranges can be reconstituted on demand.

namespace viper::codegen::x64::ra
{

namespace
{

using RegPool = std::deque<PhysReg>;

template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

/// @brief Identify general-purpose registers that must never be allocated.
/// @details The stack pointer and frame pointer are reserved by the calling
///          convention, so the allocator filters them out of the initial pools.
/// @param reg Candidate register.
/// @return @c true when @p reg is reserved.
[[nodiscard]] bool isReservedGPR(PhysReg reg) noexcept
{
    return reg == PhysReg::RSP || reg == PhysReg::RBP;
}

/// @brief Wrap a physical register into a Machine IR operand.
/// @details Converts the strongly typed @ref PhysReg enumeration into the raw
///          identifier used by Machine IR instructions so helper routines can
///          build @c MOV-like instructions without repeating casts.
/// @param cls Register class the operand belongs to.
/// @param reg Physical register identifier.
/// @return Machine operand referencing @p reg.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

} // namespace

/// @brief Create an allocator for a machine function.
/// @details The constructor caches references to the function being rewritten,
///          target ABI metadata, and liveness information.  It also precomputes
///          the register pools so @ref run can draw from ready-to-use vectors.
/// @param func Machine function to allocate.
/// @param target Target-specific register and ABI description.
/// @param intervals Live interval analysis results for @p func.
LinearScanAllocator::LinearScanAllocator(MFunction &func,
                                         const TargetInfo &target,
                                         const LiveIntervals &intervals)
    : func_(func), target_(target), intervals_(intervals)
{
    buildPools();

    // Precompute caller-saved register bitsets for O(1) lookup during CALL handling.
    // This avoids O(n) linear search through vectors on every call instruction.
    for (PhysReg reg : target_.callerSavedGPR)
        callerSavedGPRBits_.set(static_cast<std::size_t>(reg));
    for (PhysReg reg : target_.callerSavedXMM)
        callerSavedXMMBits_.set(static_cast<std::size_t>(reg));
}

/// @brief Execute the allocation pipeline over the entire function.
/// @details Iterates blocks in layout order, rewriting each instruction to use
///          physical registers while invoking the coalescer to lower PX_COPY
///          pseudos.  After each block the allocator releases any registers that
///          do not remain live into successor blocks.  The final spill-slot
///          counts are copied from the spiller before returning the result map.
/// @return Summary of virtual→physical mappings and spill requirements.
AllocationResult LinearScanAllocator::run()
{
    // Pre-pass: identify vregs that span multiple blocks.
    // Because our linear live interval analysis doesn't account for control flow,
    // vregs used in multiple blocks may be incorrectly allocated when a forward
    // jump goes from a later block (in linear order) to an earlier block. By
    // pre-marking such vregs as needing spills, we ensure that every use triggers
    // a reload from the spill slot, guaranteeing correctness.
    std::unordered_map<uint16_t, std::size_t> vregFirstBlock;
    std::unordered_set<uint16_t> crossBlockVregs;

    for (std::size_t blockIdx = 0; blockIdx < func_.blocks.size(); ++blockIdx)
    {
        const auto &block = func_.blocks[blockIdx];
        for (const auto &instr : block.instructions)
        {
            for (const auto &operand : instr.operands)
            {
                if (const auto *reg = std::get_if<OpReg>(&operand); reg && !reg->isPhys)
                {
                    auto it = vregFirstBlock.find(reg->idOrPhys);
                    if (it == vregFirstBlock.end())
                    {
                        vregFirstBlock[reg->idOrPhys] = blockIdx;
                    }
                    else if (it->second != blockIdx)
                    {
                        crossBlockVregs.insert(reg->idOrPhys);
                    }
                }
                else if (const auto *mem = std::get_if<OpMem>(&operand))
                {
                    if (!mem->base.isPhys)
                    {
                        auto it = vregFirstBlock.find(mem->base.idOrPhys);
                        if (it == vregFirstBlock.end())
                        {
                            vregFirstBlock[mem->base.idOrPhys] = blockIdx;
                        }
                        else if (it->second != blockIdx)
                        {
                            crossBlockVregs.insert(mem->base.idOrPhys);
                        }
                    }
                    if (mem->hasIndex && !mem->index.isPhys)
                    {
                        auto it = vregFirstBlock.find(mem->index.idOrPhys);
                        if (it == vregFirstBlock.end())
                        {
                            vregFirstBlock[mem->index.idOrPhys] = blockIdx;
                        }
                        else if (it->second != blockIdx)
                        {
                            crossBlockVregs.insert(mem->index.idOrPhys);
                        }
                    }
                }
            }
        }
    }

    // Pre-mark cross-block vregs as needing spills. This ensures that any use
    // will go through the reload path, preventing stale register values when
    // control flow doesn't match linear instruction order.
    // IMPORTANT: We must NOT use slot reuse for cross-block vregs because their
    // linear live intervals don't accurately represent actual lifetimes. Due to
    // control flow (especially forward jumps), two vregs with "non-overlapping"
    // linear intervals may actually be live at the same time.
    for (uint16_t vreg : crossBlockVregs)
    {
        // Determine the register class from the live interval
        const auto *interval = intervals_.lookup(vreg);
        RegClass cls = interval ? interval->cls : RegClass::GPR;

        auto &state = stateFor(cls, vreg);
        state.spill.needsSpill = true;
        // Allocate a unique spill slot for this vreg - no reuse to avoid conflicts
        spiller_.ensureSpillSlot(cls, state.spill);
    }

    Coalescer coalescer{*this, spiller_};
    for (auto &block : func_.blocks)
    {
        processBlock(block, coalescer);
        releaseActiveForBlock(block);
    }
    result_.spillSlotsGPR = spiller_.gprSlots();
    result_.spillSlotsXMM = spiller_.xmmSlots();
    return result_;
}

/// @brief Populate the per-class register pools from target metadata.
/// @details Caller-saved and callee-saved registers are concatenated so the
///          allocator can draw from a single vector per class.  Reserved
///          registers (stack and frame pointers) are filtered out to avoid
///          accidental allocation.
void LinearScanAllocator::buildPools()
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

/// @brief Access the register pool matching a class.
/// @param cls Register class to query.
/// @return Mutable vector of available physical registers.
std::deque<PhysReg> &LinearScanAllocator::poolFor(RegClass cls)
{
    return cls == RegClass::GPR ? freeGPR_ : freeXMM_;
}

/// @brief Access the active list for a given register class.
/// @param cls Register class to query.
/// @return Mutable list of virtual registers currently holding physical regs.
std::unordered_set<uint16_t> &LinearScanAllocator::activeFor(RegClass cls)
{
    return cls == RegClass::GPR ? activeGPR_ : activeXMM_;
}

/// @brief Fetch or create the allocation record for a virtual register.
/// @details Stores the register class on first use and asserts that subsequent
///          queries agree on the class, catching mismatched operand encodings.
/// @param cls Register class inferred from the current operand.
/// @param id Virtual register identifier.
/// @return Mutable allocation state for @p id.
VirtualAllocation &LinearScanAllocator::stateFor(RegClass cls, uint16_t id)
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

/// @brief Record that a virtual register currently owns a physical register.
/// @details Active sets ensure the allocator can pick eviction victims and
///          release registers at block boundaries. Uses unordered_set for O(1)
///          insert instead of O(n) linear search.
/// @param cls Register class of the active value.
/// @param id Virtual register identifier.
void LinearScanAllocator::addActive(RegClass cls, uint16_t id)
{
    activeFor(cls).insert(id);
}

/// @brief Remove a virtual register from the active set.
/// @details Called when a value goes dead or is explicitly spilled so future
///          spill victims do not consider the register. Uses unordered_set for
///          O(1) erase instead of O(n) remove-erase idiom.
/// @param cls Register class of the active value.
/// @param id Virtual register identifier to remove.
void LinearScanAllocator::removeActive(RegClass cls, uint16_t id)
{
    activeFor(cls).erase(id);
}

/// @brief Lease a physical register from the free pool.
/// @details If the pool is empty the allocator triggers a spill to free one
///          register, appending spill code to @p prefix.  Once a register is
///          available, it is removed from the front of the pool to preserve a
///          deterministic allocation order.
/// @param cls Register class to allocate.
/// @param prefix Instruction list receiving any required spill code.
/// @return Physical register assigned to the caller.
PhysReg LinearScanAllocator::takeRegister(RegClass cls, std::vector<MInstr> &prefix)
{
    auto &pool = poolFor(cls);
    if (pool.empty())
    {
        spillOne(cls, prefix);
    }
    assert(!pool.empty() && "register pool exhausted");
    const PhysReg reg = pool.front();
    pool.pop_front();  // O(1) instead of O(n) erase(begin())
    return reg;
}

/// @brief Return a physical register to the free pool.
/// @details Used after temporary loads or at block exits to recycle registers
///          for future allocations.
/// @param phys Register being released.
/// @param cls Class of @p phys.
void LinearScanAllocator::releaseRegister(PhysReg phys, RegClass cls)
{
    poolFor(cls).push_back(phys);
}

/// @brief Spill one active virtual register to free a physical register.
/// @details The allocator selects the earliest active value, requests that the
///          spiller emit a store, and returns the freed register to the pool.
///          Values that already lack a physical register are skipped to avoid
///          redundant work. Uses lifetime-based slot reuse when interval info
///          is available to reduce stack frame size.
/// @param cls Register class experiencing pressure.
/// @param prefix Instruction list capturing generated spill code.
void LinearScanAllocator::spillOne(RegClass cls, std::vector<MInstr> &prefix)
{
    auto &active = activeFor(cls);
    if (active.empty())
    {
        return;
    }
    // Use begin() iterator since unordered_set doesn't have front()
    auto victimIt = active.begin();
    const uint16_t victimId = *victimIt;
    active.erase(victimIt);
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
    // Use lifetime-based slot reuse when interval info is available
    const auto *interval = intervals_.lookup(victimId);
    if (interval)
    {
        spiller_.spillValueWithReuse(cls, victimId, victim, poolFor(cls), prefix, result_,
                                     interval->start, interval->end);
    }
    else
    {
        spiller_.spillValue(cls, victimId, victim, poolFor(cls), prefix, result_);
    }
}

/// @brief Release registers for vregs whose live intervals have ended.
/// @details At each instruction, we check all active vregs to see if their
///          interval ends at or before the current instruction. If so, the vreg
///          is no longer live and its physical register can be returned to the
///          free pool for reuse. This is essential for correct register reuse
///          within basic blocks.
void LinearScanAllocator::expireIntervals()
{
    // Collect expired vregs (can't modify active set while iterating)
    std::vector<uint16_t> expiredGPR{};
    std::vector<uint16_t> expiredXMM{};

    for (auto vreg : activeGPR_)
    {
        const auto *interval = intervals_.lookup(vreg);
        // Expire if interval ends at or before current instruction
        if (interval && interval->end <= currentInstrIdx_)
        {
            expiredGPR.push_back(vreg);
        }
    }

    for (auto vreg : activeXMM_)
    {
        const auto *interval = intervals_.lookup(vreg);
        if (interval && interval->end <= currentInstrIdx_)
        {
            expiredXMM.push_back(vreg);
        }
    }

    // Now release the expired vregs
    for (auto vreg : expiredGPR)
    {
        auto it = states_.find(vreg);
        if (it != states_.end() && it->second.hasPhys)
        {
            releaseRegister(it->second.phys, RegClass::GPR);
            it->second.hasPhys = false;
        }
        removeActive(RegClass::GPR, vreg);
    }

    for (auto vreg : expiredXMM)
    {
        auto it = states_.find(vreg);
        if (it != states_.end() && it->second.hasPhys)
        {
            releaseRegister(it->second.phys, RegClass::XMM);
            it->second.hasPhys = false;
        }
        removeActive(RegClass::XMM, vreg);
    }
}

/// @brief Rewrite a block so each instruction uses allocated registers.
/// @details The method iterates the block, lowering PX_COPY pseudos via the
///          coalescer and handling other instructions by:
///          1. Classifying operand roles (use/def).
///          2. Ensuring operands have physical registers, emitting loads or
///             spills into prefix/suffix buffers as needed.
///          3. Releasing scratch registers after their final use.
///          The rewritten instruction sequence replaces the original block
///          contents in place.
/// @param block Machine basic block being processed.
/// @param coalescer Helper that lowers PX_COPY instructions.
void LinearScanAllocator::processBlock(MBasicBlock &block, Coalescer &coalescer)
{
    std::vector<MInstr> rewritten{};
    rewritten.reserve(block.instructions.size());

    for (const auto &instr : block.instructions)
    {
        // Expire vregs whose live intervals have ended before this instruction.
        // This ensures their physical registers are returned to the free pool for reuse.
        expireIntervals();

        if (instr.opcode == MOpcode::PX_COPY)
        {
            coalescer.lower(instr, rewritten);
            ++currentInstrIdx_;
            continue;
        }

        // Before processing operands, check if this instruction writes to a physical
        // register. This handles two cases:
        // 1. Call argument setup (MOVrr/MOVri to arg registers): reserve the register
        //    so spill reloads don't clobber it before the CALL.
        // 2. Any write to a physical register: if a vreg is currently assigned to that
        //    register and is still live, spill it to avoid corruption.
        std::vector<MInstr> prefix{};
        if ((instr.opcode == MOpcode::MOVrr || instr.opcode == MOpcode::MOVri ||
             instr.opcode == MOpcode::LEA) &&
            !instr.operands.empty())
        {
            if (const auto *destReg = std::get_if<OpReg>(&instr.operands[0]))
            {
                if (destReg->isPhys)
                {
                    const PhysReg physDest = static_cast<PhysReg>(destReg->idOrPhys);

                    // For MOVrr, check if source is the same vreg assigned to dest.
                    // If so, no spill is needed (we're copying a value to its own register).
                    uint16_t srcVreg = std::numeric_limits<uint16_t>::max();
                    if (instr.opcode == MOpcode::MOVrr && instr.operands.size() > 1)
                    {
                        if (const auto *srcReg = std::get_if<OpReg>(&instr.operands[1]))
                        {
                            if (!srcReg->isPhys)
                            {
                                srcVreg = srcReg->idOrPhys;
                            }
                        }
                    }

                    // Check if any vreg is currently assigned to this physical register
                    // and spill it before we clobber the register.
                    for (auto vreg : activeGPR_)
                    {
                        auto it = states_.find(vreg);
                        if (it != states_.end() && it->second.hasPhys &&
                            it->second.phys == physDest)
                        {
                            // Skip spill if source vreg == vreg in dest register (no-op move)
                            if (vreg == srcVreg)
                            {
                                break;
                            }

                            auto &state = it->second;
                            // Check if the value is still needed after this point.
                            // If we don't have interval info, conservatively assume
                            // the value might be needed and spill it to avoid data loss.
                            const auto *interval = intervals_.lookup(vreg);
                            const bool valueNeeded = !interval || interval->end > currentInstrIdx_;
                            if (valueNeeded)
                            {
                                // Use lifetime-based slot reuse when interval is available
                                if (interval)
                                {
                                    spiller_.ensureSpillSlotWithReuse(RegClass::GPR, state.spill,
                                                                      interval->start, interval->end);
                                }
                                else
                                {
                                    spiller_.ensureSpillSlot(RegClass::GPR, state.spill);
                                }
                                state.spill.needsSpill = true;
                                prefix.push_back(
                                    spiller_.makeStore(RegClass::GPR, state.spill, state.phys));
                            }
                            releaseRegister(state.phys, RegClass::GPR);
                            state.hasPhys = false;
                            removeActive(RegClass::GPR, vreg);
                            break; // Only one vreg can be in a physical register
                        }
                    }

                    // Reserve argument registers for call setup
                    if (isArgumentRegister(physDest))
                    {
                        reserveForCall(physDest);
                    }
                }
            }
        }
        std::vector<MInstr> suffix{};
        std::vector<ScratchRelease> scratch{};
        MInstr current = instr;
        auto roles = classifyOperands(current);

        for (std::size_t idx = 0; idx < current.operands.size(); ++idx)
        {
            handleOperand(current.operands[idx], roles[idx], prefix, suffix, scratch);
        }

        // Handle CALL: values in caller-saved registers are clobbered
        // Spill them BEFORE the call and mark for reload on next use
        if (instr.opcode == MOpcode::CALL)
        {
            // Use precomputed bitsets for O(1) caller-saved lookup instead of O(n) linear search
            auto isCallerSaved = [this](PhysReg reg, RegClass cls)
            {
                const auto &bits = cls == RegClass::GPR ? callerSavedGPRBits_ : callerSavedXMMBits_;
                return bits.test(static_cast<std::size_t>(reg));
            };

            // Collect vregs to spill (can't modify active list while iterating)
            std::vector<uint16_t> gprToSpill{};
            std::vector<uint16_t> xmmToSpill{};

            // Process GPR values - spill before call
            for (auto vreg : activeGPR_)
            {
                auto it = states_.find(vreg);
                if (it == states_.end() || !it->second.hasPhys)
                {
                    continue;
                }
                auto &state = it->second;
                if (!isCallerSaved(state.phys, RegClass::GPR))
                {
                    continue;
                }
                // Check if this value is used after the call.
                // If we don't have interval info, conservatively spill to avoid data loss.
                const auto *interval = intervals_.lookup(vreg);
                if (interval && interval->end <= currentInstrIdx_ + 1)
                {
                    continue;  // Only skip if interval confirms value is dead after call
                }
                gprToSpill.push_back(vreg);
            }

            // Process XMM values - spill before call
            for (auto vreg : activeXMM_)
            {
                auto it = states_.find(vreg);
                if (it == states_.end() || !it->second.hasPhys)
                {
                    continue;
                }
                auto &state = it->second;
                if (!isCallerSaved(state.phys, RegClass::XMM))
                {
                    continue;
                }
                // If we don't have interval info, conservatively spill to avoid data loss.
                const auto *interval = intervals_.lookup(vreg);
                if (interval && interval->end <= currentInstrIdx_ + 1)
                {
                    continue;  // Only skip if interval confirms value is dead after call
                }
                xmmToSpill.push_back(vreg);
            }

            // Now spill the collected values using lifetime-based slot reuse
            for (auto vreg : gprToSpill)
            {
                auto &state = states_[vreg];
                const auto *interval = intervals_.lookup(vreg);
                if (interval)
                {
                    spiller_.ensureSpillSlotWithReuse(RegClass::GPR, state.spill,
                                                      interval->start, interval->end);
                }
                else
                {
                    spiller_.ensureSpillSlot(RegClass::GPR, state.spill);
                }
                state.spill.needsSpill = true;
                prefix.push_back(spiller_.makeStore(RegClass::GPR, state.spill, state.phys));
                releaseRegister(state.phys, RegClass::GPR);
                state.hasPhys = false;
                removeActive(RegClass::GPR, vreg);
            }

            for (auto vreg : xmmToSpill)
            {
                auto &state = states_[vreg];
                const auto *interval = intervals_.lookup(vreg);
                if (interval)
                {
                    spiller_.ensureSpillSlotWithReuse(RegClass::XMM, state.spill,
                                                      interval->start, interval->end);
                }
                else
                {
                    spiller_.ensureSpillSlot(RegClass::XMM, state.spill);
                }
                state.spill.needsSpill = true;
                prefix.push_back(spiller_.makeStore(RegClass::XMM, state.spill, state.phys));
                releaseRegister(state.phys, RegClass::XMM);
                state.hasPhys = false;
                removeActive(RegClass::XMM, vreg);
            }

            // Release the argument registers that were reserved during call setup
            // back to the pool now that the call is complete.
            releaseCallReserved();
        }

        // Handle CQO: implicitly writes to RDX (sign-extends RAX into RDX:RAX)
        // Any vreg currently in RDX must be spilled before CQO executes
        if (instr.opcode == MOpcode::CQO)
        {
            for (auto vreg : activeGPR_)
            {
                auto it = states_.find(vreg);
                if (it == states_.end() || !it->second.hasPhys)
                {
                    continue;
                }
                auto &state = it->second;
                if (state.phys != PhysReg::RDX)
                {
                    continue;
                }
                // RDX will be clobbered by CQO - spill if value is still needed
                const auto *interval = intervals_.lookup(vreg);
                if (interval && interval->end <= currentInstrIdx_ + 1)
                {
                    // Value is dead after CQO, just release the register
                    releaseRegister(state.phys, RegClass::GPR);
                    state.hasPhys = false;
                    removeActive(RegClass::GPR, vreg);
                }
                else
                {
                    // Value is needed later - spill it
                    spiller_.ensureSpillSlot(RegClass::GPR, state.spill);
                    state.spill.needsSpill = true;
                    prefix.push_back(spiller_.makeStore(RegClass::GPR, state.spill, state.phys));
                    releaseRegister(state.phys, RegClass::GPR);
                    state.hasPhys = false;
                    removeActive(RegClass::GPR, vreg);
                }
                break; // Only one vreg can be in RDX
            }
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

        ++currentInstrIdx_;
    }

    block.instructions = std::move(rewritten);
}

/// @brief Release or spill registers at block boundaries.
/// @details Called after rewriting a block. Values whose live intervals end
///          before the current instruction have their registers released.
///          Values that are live across block boundaries are spilled to memory
///          so that successor blocks can reload them. This ensures cross-block
///          liveness is handled correctly without relying on registers persisting.
/// @param block The block that was just processed, to which spills are inserted.
void LinearScanAllocator::releaseActiveForBlock(MBasicBlock &block)
{
    // Helper to check if an instruction is a terminator
    auto isTerminator = [](MOpcode opc)
    { return opc == MOpcode::JMP || opc == MOpcode::JCC || opc == MOpcode::RET; };

    // Find insertion point - before the terminator if present
    std::size_t insertPos = block.instructions.size();
    if (!block.instructions.empty() && isTerminator(block.instructions.back().opcode))
    {
        insertPos = block.instructions.size() - 1;
        // Check if there are two terminators (JCC followed by JMP)
        if (insertPos > 0 && isTerminator(block.instructions[insertPos - 1].opcode))
        {
            insertPos--;
        }
    }

    std::vector<MInstr> spills{};

    // Process GPR values at block boundaries.
    // Cross-block vregs (marked with needsSpill=true in the pre-pass) are already
    // handled by processRegOperand which emits stores on defs. We only need to
    // emit an additional store here if the current register value hasn't been
    // stored yet (i.e., the vreg was modified since last store).
    // Single-block vregs (needsSpill=false) don't need spilling since no other
    // block can access them.
    for (auto vreg : activeGPR_)
    {
        auto it = states_.find(vreg);
        if (it == states_.end() || !it->second.hasPhys)
        {
            continue;
        }

        auto &state = it->second;
        // For cross-block vregs that are already marked for spilling, we need to
        // ensure the current value is in memory. The processRegOperand handles
        // stores on defs, so the spill slot should be up to date. We just need
        // to release the register.
        // For single-block vregs, we can just release without spilling.
        releaseRegister(it->second.phys, RegClass::GPR);
        it->second.hasPhys = false;
    }
    activeGPR_.clear();

    // Process XMM values - same approach as GPR
    for (auto vreg : activeXMM_)
    {
        auto it = states_.find(vreg);
        if (it == states_.end() || !it->second.hasPhys)
        {
            continue;
        }

        releaseRegister(it->second.phys, RegClass::XMM);
        it->second.hasPhys = false;
    }
    activeXMM_.clear();

    // Insert spills before the terminator(s)
    if (!spills.empty())
    {
        block.instructions.insert(block.instructions.begin() + static_cast<long>(insertPos),
                                  std::make_move_iterator(spills.begin()),
                                  std::make_move_iterator(spills.end()));
    }
}

/// @brief Determine whether operands are read, written, or both.
/// @details The classification drives register materialisation: uses require
///          loads while defs may force spills after the instruction executes.
///          The switch enumerates the instructions emitted during Phase A of the
///          backend.
/// @param instr Instruction whose operands are being analysed.
/// @return Vector describing the role of each operand.
std::vector<LinearScanAllocator::OperandRole> LinearScanAllocator::classifyOperands(
    const MInstr &instr) const
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
        case MOpcode::MOVmr:
            // Load from memory to register: dest is def-only, source memory is use
            if (!roles.empty())
            {
                roles[0] = OperandRole{false, true};
            }
            // operand 1 (memory) base/index handled by handleOperand
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
        case MOpcode::CMOVNErr:
        case MOpcode::ANDrr:
        case MOpcode::ORrr:
        case MOpcode::XORrr:
        case MOpcode::SHLrc:
        case MOpcode::SHRrc:
        case MOpcode::SARrc:
            if (!roles.empty())
            {
                roles[0] = OperandRole{true, true};
            }
            if (roles.size() > 1)
            {
                roles[1] = OperandRole{true, false};
            }
            break;
        case MOpcode::SHLri:
        case MOpcode::SHRri:
        case MOpcode::SARri:
        case MOpcode::ANDri:
        case MOpcode::ORri:
        case MOpcode::XORri:
            if (!roles.empty())
            {
                roles[0] = OperandRole{true, true};
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
            // SETcc has operands: (condCode:Imm, dest:RegOrMem)
            // The condition code is read-only, the destination is write-only
            if (!roles.empty())
            {
                roles[0] = OperandRole{true, false}; // condition code is read
            }
            if (roles.size() > 1)
            {
                roles[1] = OperandRole{false, true}; // destination is write
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

/// @brief Ensure an operand has a valid physical encoding.
/// @details Delegates to @ref processRegOperand for register operands and
///          recursively handles memory operands by processing their base
///          registers.  Immediate-like operands require no work.
/// @param operand Operand being rewritten in place.
/// @param role Use/def classification for @p operand.
/// @param prefix Instruction list receiving pre-instruction loads or spills.
/// @param suffix Instruction list receiving post-instruction spills.
/// @param scratch Scratch register tracker used to release temporaries.
void LinearScanAllocator::handleOperand(Operand &operand,
                                        const OperandRole &role,
                                        std::vector<MInstr> &prefix,
                                        std::vector<MInstr> &suffix,
                                        std::vector<ScratchRelease> &scratch)
{
    std::visit(Overload{[&](OpReg &reg) { processRegOperand(reg, role, prefix, suffix, scratch); },
                        [&](OpMem &mem)
                        {
                            OperandRole baseRole{true, false};
                            processRegOperand(mem.base, baseRole, prefix, suffix, scratch);
                            // Also process the index register if present
                            if (mem.hasIndex)
                            {
                                processRegOperand(mem.index, baseRole, prefix, suffix, scratch);
                            }
                        },
                        [](auto &) {}},
               operand);
}

/// @brief Rewrite a virtual register operand into a physical register operand.
/// @details Handles three scenarios:
///          1. Already-spilled values: reload into a scratch register (for uses)
///             and/or schedule stores (for defs).
///          2. First-time allocations: lease a register, update maps, and mark
///             the register as active.
///          3. Previously allocated values: reuse the recorded physical
///             register.  Any scratch registers acquired are tracked for later
///             release.
/// @param reg Operand mutated in place.
/// @param role Use/def role for @p reg.
/// @param prefix List receiving pre-instruction loads.
/// @param suffix List receiving post-instruction spills.
/// @param scratch Scratch register tracker for release bookkeeping.
void LinearScanAllocator::processRegOperand(OpReg &reg,
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
    if (state.spill.needsSpill)
    {
        spiller_.ensureSpillSlot(state.cls, state.spill);
        const PhysReg phys = takeRegister(state.cls, prefix);
        if (role.isUse)
        {
            prefix.push_back(spiller_.makeLoad(state.cls, phys, state.spill));
        }
        if (role.isDef)
        {
            suffix.push_back(spiller_.makeStore(state.cls, state.spill, phys));
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

    reg = makePhysReg(state.cls, static_cast<uint16_t>(state.phys));
}

/// @brief Build a register-to-register move for a specific class.
/// @details Used by the coalescer and allocator to move values without
///          duplicating opcode selection logic.
/// @param cls Register class describing the move type.
/// @param dst Destination physical register.
/// @param src Source physical register.
/// @return Machine instruction encoding the move.
MInstr LinearScanAllocator::makeMove(RegClass cls, PhysReg dst, PhysReg src) const
{
    if (cls == RegClass::GPR)
    {
        return MInstr::make(MOpcode::MOVrr, {makePhysOperand(cls, dst), makePhysOperand(cls, src)});
    }
    return MInstr::make(MOpcode::MOVSDrr, {makePhysOperand(cls, dst), makePhysOperand(cls, src)});
}

/// @brief Check if a physical register is an argument register for the current ABI.
/// @details Used to detect when call argument registers are being set so they can be
///          reserved and not used for spill reloads during call setup.
/// @param reg Physical register to check.
/// @return @c true if @p reg is an argument-passing register.
bool LinearScanAllocator::isArgumentRegister(PhysReg reg) const
{
    for (std::size_t i = 0; i < target_.maxGPRArgs && i < target_.intArgOrder.size(); ++i)
    {
        if (target_.intArgOrder[i] == reg)
        {
            return true;
        }
    }
    return false;
}

/// @brief Reserve an argument register during call setup.
/// @details Removes the register from the free pool and records it so it can be
///          released after the CALL instruction is processed. This prevents spill
///          reloads from clobbering argument values during call setup.
/// @param reg Physical register to reserve.
void LinearScanAllocator::reserveForCall(PhysReg reg)
{
    // Check if already reserved
    if (std::find(reservedForCall_.begin(), reservedForCall_.end(), reg) != reservedForCall_.end())
    {
        return;
    }
    // Remove from free pool
    auto &pool = poolFor(RegClass::GPR);
    auto it = std::find(pool.begin(), pool.end(), reg);
    if (it != pool.end())
    {
        pool.erase(it);
        reservedForCall_.push_back(reg);
    }
}

/// @brief Release all reserved argument registers back to the pool.
/// @details Called after a CALL instruction is processed to make argument
///          registers available for subsequent allocations.
void LinearScanAllocator::releaseCallReserved()
{
    for (auto reg : reservedForCall_)
    {
        poolFor(RegClass::GPR).push_back(reg);
    }
    reservedForCall_.clear();
}

} // namespace viper::codegen::x64::ra
