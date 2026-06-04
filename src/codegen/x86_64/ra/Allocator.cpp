//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/ra/Allocator.cpp
// Purpose: Implement the linear-scan allocation phase which assigns physical
//          registers, inserts spill code, and lowers PX_COPY bundles for the
//          x86-64 backend.
// Key invariants:
//   - Register pools are deterministically populated from the target ABI.
//   - Allocation proceeds in block order, releasing all live values at boundaries.
// Ownership/Lifetime:
//   - Mutates MIR blocks in place; returns AllocationResult to the caller.
// Links: codegen/x86_64/ra/Allocator.hpp,
//        codegen/x86_64/ra/Coalescer.hpp,
//        codegen/x86_64/OperandRoles.hpp
//
//===----------------------------------------------------------------------===//

#include "Allocator.hpp"

#include "Coalescer.hpp"
#include "codegen/x86_64/OperandRoles.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>

/// @file
/// @brief Implements the x86-64 linear-scan register allocator.
/// @details The allocator walks Machine IR blocks in order, leasing physical
///          registers from ABI-configured pools, spilling values when pressure
///          grows, and invoking the coalescer to expand PX_COPY pseudos.  The
///          implementation maintains per-class pools and active lists so live
///          ranges can be reconstituted on demand.

namespace viper::codegen::x64::ra {

namespace {

using RegPool = std::deque<PhysReg>;

template <typename... Ts> struct Overload : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

/// @brief Identify general-purpose registers that must never be allocated.
/// @details The stack/frame pointers are reserved by the calling convention.
///          R10/R11 are backend scratch registers used by multi-instruction
///          lowering sequences; keeping them out of the general pool prevents a
///          later vreg reload from overwriting scratch state before it is read.
/// @param reg Candidate register.
/// @return @c true when @p reg is reserved.
[[nodiscard]] bool isReservedGPR(PhysReg reg) noexcept {
    return reg == PhysReg::RSP || reg == PhysReg::RBP || reg == PhysReg::R10 || reg == PhysReg::R11;
}

/// @brief Wrap a physical register into a Machine IR operand.
/// @details Converts the strongly typed @ref PhysReg enumeration into the raw
///          identifier used by Machine IR instructions so helper routines can
///          build @c MOV-like instructions without repeating casts.
/// @param cls Register class the operand belongs to.
/// @param reg Physical register identifier.
/// @return Machine operand referencing @p reg.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg) {
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

/// @brief Description of a physical register clobbered by an instruction.
struct PhysClobber {
    PhysReg reg{PhysReg::RAX};   ///< Clobbered physical register.
    RegClass cls{RegClass::GPR}; ///< Register class (GPR or XMM).
};

/// @brief Append @p reg to @p clobbers, computing its class and de-duplicating.
/// @details Allocator code needs a set-like view of every physical clobber
///          implied by an instruction so it can spill conflicting live vregs.
///          We use a vector (compact, cache-friendly) and dedupe linearly —
///          the typical clobber count per instruction is < 4.
void addPhysClobber(std::vector<PhysClobber> &clobbers, PhysReg reg) {
    const RegClass cls = isXMM(reg) ? RegClass::XMM : RegClass::GPR;
    const auto duplicate = std::any_of(clobbers.begin(), clobbers.end(), [&](const auto &item) {
        return item.reg == reg && item.cls == cls;
    });
    if (!duplicate) {
        clobbers.push_back(PhysClobber{reg, cls});
    }
}

/// @brief Return a compact register-class name for allocator diagnostics.
[[nodiscard]] const char *regClassName(RegClass cls) noexcept {
    return cls == RegClass::XMM ? "XMM" : "GPR";
}

/// @brief Compute the full set of physical registers an instruction overwrites.
/// @details Combines two sources: explicit def-position physical operands, and
///          implicit clobbers for opcodes that touch fixed registers (CQO,
///          IDIVrm, DIVrm — all of which clobber RAX/RDX). The result drives
///          the allocator's "spill any live vreg that lands here" logic.
std::vector<PhysClobber> collectPhysicalClobbers(const MInstr &instr) {
    std::vector<PhysClobber> clobbers;
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isUse;
        if (!isDef)
            continue;
        const auto *reg = std::get_if<OpReg>(&instr.operands[idx]);
        if (reg && reg->isPhys) {
            addPhysClobber(clobbers, static_cast<PhysReg>(reg->idOrPhys));
        }
    }

    if (instr.opcode == MOpcode::CQO) {
        addPhysClobber(clobbers, PhysReg::RDX);
    } else if (instr.opcode == MOpcode::IDIVrm || instr.opcode == MOpcode::DIVrm) {
        addPhysClobber(clobbers, PhysReg::RAX);
        addPhysClobber(clobbers, PhysReg::RDX);
    }
    return clobbers;
}

/// @brief Return the source vreg of a copy instruction (or @c UINT16_MAX).
/// @details Used when scanning for a MOVrr/MOVSDrr whose destination is being
///          allocated — if the source is a virtual register we can sometimes
///          coalesce by giving the destination the same physical register.
/// @return Source vreg id, or @c UINT16_MAX when the instruction is not a
///         vreg-to-anything copy.
uint16_t passthroughSourceVReg(const MInstr &instr) {
    if (instr.opcode != MOpcode::MOVrr && instr.opcode != MOpcode::MOVSDrr)
        return std::numeric_limits<uint16_t>::max();
    if (instr.operands.size() < 2)
        return std::numeric_limits<uint16_t>::max();
    const auto *srcReg = std::get_if<OpReg>(&instr.operands[1]);
    if (!srcReg || srcReg->isPhys)
        return std::numeric_limits<uint16_t>::max();
    return srcReg->idOrPhys;
}

/// @brief Predicate: is @p instr a no-op physical move into @p physDest?
/// @details After allocation, a MOVrr whose source and destination
///          resolve to the same physical register is a no-op and can be
///          discarded. The check is conservative — both operands must be
///          the same physical register of the same class.
bool isIdentityPhysicalMove(const MInstr &instr, PhysReg physDest) {
    if (instr.opcode != MOpcode::MOVrr && instr.opcode != MOpcode::MOVSDrr)
        return false;
    if (instr.operands.size() < 2)
        return false;
    const auto *dst = std::get_if<OpReg>(&instr.operands[0]);
    const auto *src = std::get_if<OpReg>(&instr.operands[1]);
    return dst && src && dst->isPhys && src->isPhys &&
           static_cast<PhysReg>(dst->idOrPhys) == physDest && dst->cls == src->cls &&
           dst->idOrPhys == src->idOrPhys;
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
    : func_(func), target_(target), intervals_(intervals) {
    buildPools();

    // Precompute caller-saved register bitsets for O(1) lookup during CALL handling.
    // This avoids O(n) linear search through vectors on every call instruction.
    for (PhysReg reg : target_.callerSavedGPR)
        callerSavedGPRBits_.set(static_cast<std::size_t>(reg));
    for (PhysReg reg : target_.callerSavedFPR)
        callerSavedFPRBits_.set(static_cast<std::size_t>(reg));
}

/// @brief Execute the allocation pipeline over the entire function.
/// @details Iterates blocks in layout order, rewriting each instruction to use
///          physical registers while invoking the coalescer to lower PX_COPY
///          pseudos.  After each block the allocator releases any registers that
///          do not remain live into successor blocks.  The final spill-slot
///          counts are copied from the spiller before returning the result map.
/// @return Summary of virtual→physical mappings and spill requirements.
AllocationResult LinearScanAllocator::run() {
    // Compute CFG-aware liveness: builds control-flow graph from JMP/JCC
    // terminators and solves the standard backward dataflow equations to
    // produce per-block liveIn/liveOut sets. This replaces the conservative
    // "unconditional spill" hack that previously force-spilled ALL cross-block
    // vregs.
    liveness_.run(func_);

    crossBlockSpillVRegs_.clear();

    // Pre-pass: mark vregs that cross non-carryable CFG boundaries as needing
    // spill homes. Straight-line single-predecessor successors can carry values
    // in registers directly, but joins, backedges, and out-of-order successors
    // still need a memory home for correctness.
    for (std::size_t bi = 0; bi < func_.blocks.size(); ++bi) {
        if (canCarryIntoNextBlock(bi)) {
            continue;
        }
        for (uint16_t vreg : liveness_.liveOut(bi)) {
            crossBlockSpillVRegs_.insert(vreg);
            const auto *interval = intervals_.lookup(vreg);
            RegClass cls = interval ? interval->cls : RegClass::GPR;

            auto &state = stateFor(cls, vreg);
            if (!state.spill.needsSpill) {
                state.spill.needsSpill = true;
                spiller_.ensureSpillSlot(cls, state.spill);
            }
        }
    }

    Coalescer coalescer{*this, spiller_};
    for (std::size_t bi = 0; bi < func_.blocks.size(); ++bi) {
        currentBlockIdx_ = bi;
        processBlock(func_.blocks[bi], coalescer);
        releaseActiveForBlock(func_.blocks[bi], bi);
    }
    result_.spillSlotsGPR = spiller_.gprSlots();
    result_.spillSlotsXMM = spiller_.xmmSlots();
    return result_;
}

bool LinearScanAllocator::canCarryIntoNextBlock(std::size_t blockIdx) const {
    if (blockIdx + 1 >= func_.blocks.size()) {
        return false;
    }

    const auto &succs = liveness_.successors(blockIdx);
    if (succs.size() != 1 || succs.front() != blockIdx + 1) {
        return false;
    }

    const auto &preds = liveness_.predecessors(blockIdx + 1);
    return preds.size() == 1 && preds.front() == blockIdx;
}

/// @brief Populate the per-class register pools from target metadata.
/// @details Caller-saved and callee-saved registers are concatenated so the
///          allocator can draw from a single vector per class.  Reserved
///          registers (stack and frame pointers) are filtered out to avoid
///          accidental allocation.
void LinearScanAllocator::buildPools() {
    auto appendRegs = [](RegPool &pool, const std::vector<PhysReg> &regs) {
        pool.insert(pool.end(), regs.begin(), regs.end());
    };

    appendRegs(freeGPR_, target_.callerSavedGPR);
    appendRegs(freeGPR_, target_.calleeSavedGPR);
    freeGPR_.erase(std::remove_if(freeGPR_.begin(),
                                  freeGPR_.end(),
                                  [](PhysReg reg) { return isReservedGPR(reg); }),
                   freeGPR_.end());

    appendRegs(freeXMM_, target_.callerSavedFPR);
    appendRegs(freeXMM_, target_.calleeSavedFPR);
}

/// @brief Access the register pool matching a class.
/// @param cls Register class to query.
/// @return Mutable vector of available physical registers.
std::deque<PhysReg> &LinearScanAllocator::poolFor(RegClass cls) {
    return cls == RegClass::GPR ? freeGPR_ : freeXMM_;
}

/// @brief Access the active list for a given register class.
/// @param cls Register class to query.
/// @return Mutable list of virtual registers currently holding physical regs.
std::unordered_set<uint16_t> &LinearScanAllocator::activeFor(RegClass cls) {
    return cls == RegClass::GPR ? activeGPR_ : activeXMM_;
}

/// @brief Fetch or create the allocation record for a virtual register.
/// @details Stores the register class on first use and asserts that subsequent
///          queries agree on the class, catching mismatched operand encodings.
/// @param cls Register class inferred from the current operand.
/// @param id Virtual register identifier.
/// @return Mutable allocation state for @p id.
VirtualAllocation &LinearScanAllocator::stateFor(RegClass cls, uint16_t id) {
    auto [it, inserted] = states_.try_emplace(id);
    auto &state = it->second;
    if (inserted) {
        state.cls = cls;
        state.seen = true;
    } else {
        state.seen = true;
        if (state.cls != cls) {
            throw std::runtime_error("x86 register allocator: virtual register v" +
                                     std::to_string(id) + " reused as " + regClassName(cls) +
                                     " after being seen as " + regClassName(state.cls));
        }
    }
    return state;
}

/// @brief Record that a virtual register currently owns a physical register.
/// @details Active sets ensure the allocator can pick eviction victims and
///          release registers at block boundaries. Uses unordered_set for O(1)
///          insert instead of O(n) linear search.
/// @param cls Register class of the active value.
/// @param id Virtual register identifier.
void LinearScanAllocator::addActive(RegClass cls, uint16_t id) {
    activeFor(cls).insert(id);
}

/// @brief Remove a virtual register from the active set.
/// @details Called when a value goes dead or is explicitly spilled so future
///          spill victims do not consider the register. Uses unordered_set for
///          O(1) erase instead of O(n) remove-erase idiom.
/// @param cls Register class of the active value.
/// @param id Virtual register identifier to remove.
void LinearScanAllocator::removeActive(RegClass cls, uint16_t id) {
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
PhysReg LinearScanAllocator::takeRegister(RegClass cls, std::vector<MInstr> &prefix) {
    auto &pool = poolFor(cls);
    if (pool.empty()) {
        spillOne(cls, prefix);
    }
    if (pool.empty()) {
        throw std::runtime_error(std::string("x86 register allocator: ") + regClassName(cls) +
                                 " register pool exhausted; all active values are pinned or "
                                 "unspillable for the current instruction");
    }
    const PhysReg reg = pool.front();
    pool.pop_front(); // O(1) instead of O(n) erase(begin())
    return reg;
}

/// @brief Return a physical register to the free pool.
/// @details Used after temporary loads or at block exits to recycle registers
///          for future allocations.
/// @param phys Register being released.
/// @param cls Class of @p phys.
void LinearScanAllocator::releaseRegister(PhysReg phys, RegClass cls) {
    poolFor(cls).push_back(phys);
}

/// @brief Spill one active virtual register to free a physical register.
/// @details The allocator selects the active value whose live interval ends
///          furthest from the current point (Belady-style heuristic), requests
///          that the spiller emit a store, and returns the freed register to the
///          pool.  Values that already lack a physical register are skipped to
///          avoid redundant work. Uses lifetime-based slot reuse when interval
///          info is available to reduce stack frame size.
/// @param cls Register class experiencing pressure.
/// @param prefix Instruction list capturing generated spill code.
void LinearScanAllocator::spillOne(RegClass cls, std::vector<MInstr> &prefix) {
    auto &active = activeFor(cls);
    if (active.empty()) {
        throw std::runtime_error(std::string("x86 register allocator: cannot spill from empty ") +
                                 regClassName(cls) + " active set");
    }
    // Deterministic victim selection with two-pass Belady-style heuristic:
    // Pass 1: Prefer evicting non-cached vregs (those not loaded this block) to
    //         avoid thrashing the in-block register cache.
    // Pass 2: Fall back to all active vregs if all are cached.
    // Within each pass, pick the vreg whose live interval ends furthest from the
    // current instruction. Ties are broken by vreg ID for determinism.
    uint16_t victimId = 0;
    bool found = false;
    std::size_t furthestEnd = 0;

    // Pass 1: non-cached vregs only
    for (uint16_t vreg : active) {
        auto stateIt = states_.find(vreg);
        if (stateIt == states_.end() || !stateIt->second.hasPhys)
            continue;
        if (pinnedForInstr_.contains(vreg))
            continue;
        if (stateIt->second.cachedInBlock)
            continue; // Skip cached vregs in first pass
        const auto *interval = intervals_.lookup(vreg);
        const std::size_t end = interval ? interval->end : std::numeric_limits<std::size_t>::max();
        if (!found || end > furthestEnd || (end == furthestEnd && vreg > victimId)) {
            furthestEnd = end;
            victimId = vreg;
            found = true;
        }
    }

    // Pass 2: all vregs (fallback if all active are cached)
    if (!found) {
        for (uint16_t vreg : active) {
            auto stateIt = states_.find(vreg);
            if (stateIt == states_.end() || !stateIt->second.hasPhys)
                continue;
            if (pinnedForInstr_.contains(vreg))
                continue;
            const auto *interval = intervals_.lookup(vreg);
            const std::size_t end =
                interval ? interval->end : std::numeric_limits<std::size_t>::max();
            if (!found || end > furthestEnd || (end == furthestEnd && vreg > victimId)) {
                furthestEnd = end;
                victimId = vreg;
                found = true;
            }
        }
    }
    if (!found) {
        throw std::runtime_error(std::string("x86 register allocator: no unpinned ") +
                                 regClassName(cls) + " spill victim is available");
    }
    active.erase(victimId);
    auto it = states_.find(victimId);
    if (it == states_.end()) {
        return;
    }
    auto &victim = it->second;
    if (!victim.hasPhys) {
        return;
    }
    spillActiveValue(victimId, victim, prefix);
}

void LinearScanAllocator::spillActiveValue(uint16_t vreg,
                                           VirtualAllocation &state,
                                           std::vector<MInstr> &out) {
    const auto *interval = intervals_.lookup(vreg);
    if (interval && !crossBlockSpillVRegs_.contains(vreg)) {
        spiller_.spillValueWithReuse(state.cls,
                                     vreg,
                                     state,
                                     poolFor(state.cls),
                                     out,
                                     result_,
                                     interval->start,
                                     interval->end);
        return;
    }

    spiller_.spillValue(state.cls, vreg, state, poolFor(state.cls), out, result_);
}

void LinearScanAllocator::pinInstructionVRegs(const MInstr &instr) {
    pinnedForInstr_.clear();
    for (const auto &operand : instr.operands) {
        if (const auto *reg = std::get_if<OpReg>(&operand)) {
            if (!reg->isPhys)
                pinnedForInstr_.insert(reg->idOrPhys);
            continue;
        }
        const auto *mem = std::get_if<OpMem>(&operand);
        if (!mem)
            continue;
        if (!mem->base.isPhys)
            pinnedForInstr_.insert(mem->base.idOrPhys);
        if (mem->hasIndex && !mem->index.isPhys)
            pinnedForInstr_.insert(mem->index.idOrPhys);
    }
}

/// @brief Release registers for vregs whose live intervals have ended.
/// @details At each instruction, we check all active vregs to see if their
///          interval ends at or before the current instruction. If so, the vreg
///          is no longer live and its physical register can be returned to the
///          free pool for reuse. This is essential for correct register reuse
///          within basic blocks.
void LinearScanAllocator::expireIntervals() {
    // Collect expired vregs (can't modify active set while iterating)
    std::vector<uint16_t> expiredGPR{};
    std::vector<uint16_t> expiredXMM{};

    for (auto vreg : activeGPR_) {
        const auto *interval = intervals_.lookup(vreg);
        // Expire if interval ends at or before current instruction
        if (interval && interval->end <= currentInstrIdx_) {
            expiredGPR.push_back(vreg);
        }
    }

    for (auto vreg : activeXMM_) {
        const auto *interval = intervals_.lookup(vreg);
        if (interval && interval->end <= currentInstrIdx_) {
            expiredXMM.push_back(vreg);
        }
    }

    // Now release the expired vregs
    for (auto vreg : expiredGPR) {
        auto it = states_.find(vreg);
        if (it != states_.end() && it->second.hasPhys) {
            releaseRegister(it->second.phys, RegClass::GPR);
            it->second.hasPhys = false;
            it->second.cachedInBlock = false;
        }
        removeActive(RegClass::GPR, vreg);
    }

    for (auto vreg : expiredXMM) {
        auto it = states_.find(vreg);
        if (it != states_.end() && it->second.hasPhys) {
            releaseRegister(it->second.phys, RegClass::XMM);
            it->second.hasPhys = false;
            it->second.cachedInBlock = false;
        }
        removeActive(RegClass::XMM, vreg);
    }
}

bool LinearScanAllocator::isCallerSaved(PhysReg reg, RegClass cls) const noexcept {
    const auto &bits = cls == RegClass::GPR ? callerSavedGPRBits_ : callerSavedFPRBits_;
    return bits.test(static_cast<std::size_t>(reg));
}

std::pair<bool, PhysReg> LinearScanAllocator::takeFreeCalleeSaved(RegClass cls) {
    auto &pool = poolFor(cls);
    const auto &bits = cls == RegClass::GPR ? callerSavedGPRBits_ : callerSavedFPRBits_;
    for (auto it = pool.begin(); it != pool.end(); ++it) {
        if (!bits.test(static_cast<std::size_t>(*it))) {
            PhysReg reg = *it;
            pool.erase(it);
            return {true, reg};
        }
    }
    return {false, PhysReg::RAX};
}

std::vector<uint16_t> LinearScanAllocator::collectCallerSavedToSpill(RegClass cls) const {
    std::vector<uint16_t> out;
    const auto &active = cls == RegClass::GPR ? activeGPR_ : activeXMM_;
    for (auto vreg : active) {
        auto it = states_.find(vreg);
        if (it == states_.end() || !it->second.hasPhys)
            continue;
        if (!isCallerSaved(it->second.phys, cls))
            continue;
        // If we don't have interval info, conservatively spill to avoid data loss.
        const auto *interval = intervals_.lookup(vreg);
        const bool liveOut = liveness_.liveOut(currentBlockIdx_).count(vreg) != 0;
        if (!liveOut && interval && interval->end <= currentInstrIdx_ + 1)
            continue; // Value confirmed dead after the call.
        out.push_back(vreg);
    }
    return out;
}

void LinearScanAllocator::spillOrRehomeAcrossCall(RegClass cls,
                                                  const std::vector<uint16_t> &candidates,
                                                  std::vector<MInstr> &prefix) {
    // Phase 1: Try to move each value to a free callee-saved register so it
    // survives the CALL without a memory round-trip.
    std::vector<uint16_t> stillNeedSpill;
    for (auto vreg : candidates) {
        auto &state = states_[vreg];
        auto [found, csReg] = takeFreeCalleeSaved(cls);
        if (found) {
            prefix.push_back(makeMove(cls, csReg, state.phys));
            releaseRegister(state.phys, cls);
            state.phys = csReg;
            result_.vregToPhys[vreg] = csReg;
            // Value stays active with new physical register; cachedInBlock preserved.
        } else {
            stillNeedSpill.push_back(vreg);
        }
    }

    // Phase 2: Spill remaining values to memory.
    for (auto vreg : stillNeedSpill) {
        auto &state = states_[vreg];
        spillActiveValue(vreg, state, prefix);
        removeActive(cls, vreg);
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
void LinearScanAllocator::processBlock(MBasicBlock &block, Coalescer &coalescer) {
    std::vector<MInstr> rewritten{};
    rewritten.reserve(block.instructions.size());

    for (const auto &instr : block.instructions) {
        // Expire vregs whose live intervals have ended before this instruction.
        // This ensures their physical registers are returned to the free pool for reuse.
        expireIntervals();
        pinInstructionVRegs(instr);

        if (instr.opcode == MOpcode::PX_COPY) {
            coalescer.lower(instr, rewritten);
            pinnedForInstr_.clear();
            ++currentInstrIdx_;
            continue;
        }

        // Before processing operands, check if this instruction writes to a physical
        // register. Fixed-register sequences such as division setup clobber RAX/RDX/R10
        // before normal virtual operands are rewritten, so any active value occupying
        // those registers must be spilled first.
        std::vector<MInstr> prefix{};
        const uint16_t srcVreg = passthroughSourceVReg(instr);
        for (const auto &clobber : collectPhysicalClobbers(instr)) {
            auto &activeSet = activeFor(clobber.cls);
            for (auto vreg : activeSet) {
                auto it = states_.find(vreg);
                if (it == states_.end() || !it->second.hasPhys || it->second.phys != clobber.reg) {
                    continue;
                }
                if (vreg == srcVreg || isIdentityPhysicalMove(instr, clobber.reg)) {
                    break;
                }

                auto &state = it->second;
                const auto *interval = intervals_.lookup(vreg);
                const bool valueNeeded = !interval || interval->end > currentInstrIdx_;
                if (valueNeeded) {
                    spillActiveValue(vreg, state, prefix);
                } else {
                    releaseRegister(state.phys, clobber.cls);
                    state.hasPhys = false;
                    state.cachedInBlock = false;
                }
                removeActive(clobber.cls, vreg);
                break;
            }

            if (isArgumentRegister(clobber.reg)) {
                reserveForCall(clobber.reg);
            }
        }
        std::vector<MInstr> suffix{};
        std::vector<ScratchRelease> scratch{};
        MInstr current = instr;
        auto roles = classifyOperands(current);

        for (std::size_t idx = 0; idx < current.operands.size(); ++idx) {
            handleOperand(current.operands[idx], roles[idx], prefix, suffix, scratch);
        }

        // Handle CALL: values in caller-saved registers are clobbered.
        // Spill (or re-home into callee-saved registers when free) BEFORE the call.
        if (instr.opcode == MOpcode::CALL) {
            // Snapshot active sets first — spillOrRehome mutates them.
            const auto gprCandidates = collectCallerSavedToSpill(RegClass::GPR);
            const auto xmmCandidates = collectCallerSavedToSpill(RegClass::XMM);
            spillOrRehomeAcrossCall(RegClass::GPR, gprCandidates, prefix);
            spillOrRehomeAcrossCall(RegClass::XMM, xmmCandidates, prefix);

            // Release argument registers reserved during call setup.
            releaseCallReserved();
        }

        // Handle CQO: implicitly writes to RDX (sign-extends RAX into RDX:RAX)
        // Any vreg currently in RDX must be spilled before CQO executes
        if (instr.opcode == MOpcode::CQO) {
            for (auto vreg : activeGPR_) {
                auto it = states_.find(vreg);
                if (it == states_.end() || !it->second.hasPhys) {
                    continue;
                }
                auto &state = it->second;
                if (state.phys != PhysReg::RDX) {
                    continue;
                }
                // RDX will be clobbered by CQO - spill if value is still needed
                const auto *interval = intervals_.lookup(vreg);
                if (interval && interval->end <= currentInstrIdx_ + 1) {
                    // Value is dead after CQO, just release the register
                    releaseRegister(state.phys, RegClass::GPR);
                    state.hasPhys = false;
                    state.cachedInBlock = false;
                    removeActive(RegClass::GPR, vreg);
                } else {
                    // Value is needed later - spill it
                    spillActiveValue(vreg, state, prefix);
                    removeActive(RegClass::GPR, vreg);
                }
                break; // Only one vreg can be in RDX
            }
        }

        for (auto &pre : prefix) {
            rewritten.push_back(std::move(pre));
        }
        rewritten.push_back(std::move(current));
        for (auto &suf : suffix) {
            rewritten.push_back(std::move(suf));
        }
        for (const auto &rel : scratch) {
            releaseRegister(rel.phys, rel.cls);
        }

        pinnedForInstr_.clear();
        ++currentInstrIdx_;
    }

    pinnedForInstr_.clear();
    block.instructions = std::move(rewritten);
}

/// @brief Release or spill registers at block boundaries using CFG-aware liveOut.
/// @details Called after rewriting a block. Uses the liveOut set from dataflow
///          analysis to determine which vregs need spilling. Vregs in liveOut
///          are spilled to ensure correct reload in successor blocks. Vregs not
///          in liveOut have their registers simply released.
/// @param block The block that was just processed.
/// @param blockIdx Index of the block for liveOut lookup.
void LinearScanAllocator::releaseActiveForBlock(MBasicBlock &block, std::size_t blockIdx) {
    const auto &liveOutSet = liveness_.liveOut(blockIdx);
    const bool carryToNext = canCarryIntoNextBlock(blockIdx);

    // Helper to check if an instruction is a terminator
    auto isTerminator = [](MOpcode opc) {
        return opc == MOpcode::JMP || opc == MOpcode::JCC || opc == MOpcode::RET ||
               opc == MOpcode::UD2;
    };

    // Find insertion point — before the terminator if present.
    std::size_t insertPos = block.instructions.size();
    for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
        if (isTerminator(block.instructions[idx].opcode)) {
            insertPos = idx;
            break;
        }
    }

    std::vector<MInstr> spills{};
    std::unordered_set<uint16_t> nextActiveGPR{};
    std::unordered_set<uint16_t> nextActiveXMM{};

    // Process GPR values at block boundaries.
    for (auto vreg : activeGPR_) {
        auto it = states_.find(vreg);
        if (it == states_.end() || !it->second.hasPhys)
            continue;

        auto &state = it->second;

        if (liveOutSet.count(vreg) && carryToNext) {
            if (state.spill.needsSpill) {
                state.cachedInBlock = true;
            }
            nextActiveGPR.insert(vreg);
            continue;
        }

        // If this vreg is live across the block boundary, ensure its current
        // value is stored to the spill slot.
        if (liveOutSet.count(vreg)) {
            spiller_.ensureSpillSlot(RegClass::GPR, state.spill);
            state.spill.needsSpill = true;
            spills.push_back(spiller_.makeStore(RegClass::GPR, state.spill, state.phys));
        }

        releaseRegister(state.phys, RegClass::GPR);
        state.hasPhys = false;
        state.cachedInBlock = false;
    }
    activeGPR_.swap(nextActiveGPR);

    // Process XMM values — same approach as GPR.
    for (auto vreg : activeXMM_) {
        auto it = states_.find(vreg);
        if (it == states_.end() || !it->second.hasPhys)
            continue;

        auto &state = it->second;

        if (liveOutSet.count(vreg) && carryToNext) {
            if (state.spill.needsSpill) {
                state.cachedInBlock = true;
            }
            nextActiveXMM.insert(vreg);
            continue;
        }

        if (liveOutSet.count(vreg)) {
            spiller_.ensureSpillSlot(RegClass::XMM, state.spill);
            state.spill.needsSpill = true;
            spills.push_back(spiller_.makeStore(RegClass::XMM, state.spill, state.phys));
        }

        releaseRegister(state.phys, RegClass::XMM);
        state.hasPhys = false;
        state.cachedInBlock = false;
    }
    activeXMM_.swap(nextActiveXMM);

    // Insert spills before the terminator(s).
    if (!spills.empty()) {
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
    const MInstr &instr) const {
    std::vector<OperandRole> roles(instr.operands.size(), OperandRole{false, false});
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        roles[idx] = OperandRole{isUse, isDef};
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
                                        std::vector<ScratchRelease> &scratch) {
    std::visit(Overload{[&](OpReg &reg) { processRegOperand(reg, role, prefix, suffix, scratch); },
                        [&](OpMem &mem) {
                            OperandRole baseRole{true, false};
                            processRegOperand(mem.base, baseRole, prefix, suffix, scratch);
                            // Also process the index register if present
                            if (mem.hasIndex) {
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
                                            std::vector<ScratchRelease> &scratch) {
    if (reg.isPhys) {
        return;
    }

    auto &state = stateFor(reg.cls, reg.idOrPhys);
    if (state.spill.needsSpill) {
        if (state.hasPhys && state.cachedInBlock) {
            // Already cached this block — reuse the register without reloading.
            if (role.isDef) {
                suffix.push_back(spiller_.makeStore(state.cls, state.spill, state.phys));
            }
            reg = makePhysReg(state.cls, static_cast<uint16_t>(state.phys));
            return;
        }
        // First access this block — allocate, load, and cache for subsequent uses.
        spiller_.ensureSpillSlot(state.cls, state.spill);
        const PhysReg phys = takeRegister(state.cls, prefix);
        if (role.isUse) {
            prefix.push_back(spiller_.makeLoad(state.cls, phys, state.spill));
        }
        if (role.isDef) {
            suffix.push_back(spiller_.makeStore(state.cls, state.spill, phys));
        }
        state.hasPhys = true;
        state.phys = phys;
        state.cachedInBlock = true;
        addActive(state.cls, reg.idOrPhys);
        result_.vregToPhys[reg.idOrPhys] = phys;
        reg = makePhysReg(state.cls, static_cast<uint16_t>(phys));
        return;
    }

    if (!state.hasPhys) {
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
MInstr LinearScanAllocator::makeMove(RegClass cls, PhysReg dst, PhysReg src) const {
    if (cls == RegClass::GPR) {
        return MInstr::make(MOpcode::MOVrr, {makePhysOperand(cls, dst), makePhysOperand(cls, src)});
    }
    return MInstr::make(MOpcode::MOVSDrr, {makePhysOperand(cls, dst), makePhysOperand(cls, src)});
}

/// @brief Check if a physical register is an argument register for the current ABI.
/// @details Used to detect when call argument registers are being set so they can be
///          reserved and not used for spill reloads during call setup.  Checks both
///          GPR and FP argument registers to prevent spill reloads from clobbering
///          marshalled arguments of either class before the CALL executes.
/// @param reg Physical register to check.
/// @return @c true if @p reg is an argument-passing register.
bool LinearScanAllocator::isArgumentRegister(PhysReg reg) const {
    for (std::size_t i = 0; i < target_.maxGPRArgs && i < target_.intArgOrder.size(); ++i) {
        if (target_.intArgOrder[i] == reg) {
            return true;
        }
    }
    for (std::size_t i = 0; i < target_.maxFPArgs && i < target_.f64ArgOrder.size(); ++i) {
        if (target_.f64ArgOrder[i] == reg) {
            return true;
        }
    }
    return false;
}

/// @brief Reserve an argument register during call setup.
/// @details Removes the register from the appropriate free pool (GPR or XMM) and
///          records it so it can be released after the CALL instruction is processed.
///          This prevents spill reloads from clobbering argument values during call
///          setup for both integer and floating-point arguments.
/// @param reg Physical register to reserve.
void LinearScanAllocator::reserveForCall(PhysReg reg) {
    // Linear search is fine: reservedForCall_ holds at most 6+8 argument
    // registers on x86-64, so O(n) with n<=14 beats any fancier structure.
    for (const auto &r : reservedForCall_) {
        if (r.phys == reg)
            return;
    }
    // Determine the class from the register itself.
    const RegClass cls = isXMM(reg) ? RegClass::XMM : RegClass::GPR;
    // Remove from the appropriate free pool.
    auto &pool = poolFor(cls);
    auto it = std::find(pool.begin(), pool.end(), reg);
    if (it != pool.end()) {
        pool.erase(it);
        reservedForCall_.push_back({reg, cls});
    }
}

/// @brief Release all reserved argument registers back to the pool.
/// @details Called after a CALL instruction is processed to make argument
///          registers available for subsequent allocations.  Returns each
///          register to its original class pool (GPR or XMM).
void LinearScanAllocator::releaseCallReserved() {
    for (const auto &r : reservedForCall_) {
        poolFor(r.cls).push_back(r.phys);
    }
    reservedForCall_.clear();
}

} // namespace viper::codegen::x64::ra
