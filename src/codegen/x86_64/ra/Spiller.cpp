//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/Spiller.cpp
// Purpose: Implement spill slot orchestration for the linear-scan allocator.
//          Provides helpers for reserving stack slots and emitting loads/stores
//          around Machine IR instructions when register pressure overflows.
// Key invariants: Spill slots are allocated in 8-byte increments relative to
//                 %rbp. Spill stores always precede instruction execution while
//                 loads precede uses.
// Ownership/Lifetime: The spiller mutates @ref AllocationResult to reflect
//                     spilled registers but does not own the Machine IR.
// Links: src/codegen/x86_64/ra/Spiller.hpp, src/codegen/x86_64/ra/Allocator.hpp
//
//===----------------------------------------------------------------------===//

#include "Spiller.hpp"

#include "../RegAllocLinear.hpp"
#include "Allocator.hpp"

/// @file
/// @brief Provides stack spill management utilities for linear-scan allocation.
/// @details The spiller assigns stack slots lazily, emits loads and stores as
///          register pressure demands, and exposes helpers the coalescer and
///          allocator reuse when materialising PX_COPY bundles or evicting live
///          ranges.

namespace viper::codegen::x64::ra
{

namespace
{

/// @brief Wrap a physical register in a Machine IR operand.
/// @details The helper mirrors the allocator's operand construction routine so
///          spiller-emitted loads and stores use the same operand encoding as
///          other MIR builders.  The register is cast to the underlying
///          `uint16_t` identifier required by the Machine IR type system.
/// @param cls Register class describing the operand.
/// @param reg Physical register referenced by the operand.
/// @return Machine operand representing @p reg.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

} // namespace

/// @brief Lazily assign a stack slot to a spill plan.
/// @details Spill plans capture whether a value must live in memory.  When a
///          plan is first encountered, this function allocates the next free
///          slot for the register class by bumping a class-specific counter.
///          Subsequent calls notice that @ref SpillPlan::slot is non-negative and
///          return early, ensuring each value reuses the same slot.
/// @param cls Register class whose spill storage is being provisioned.
/// @param plan Spill descriptor that records slot ownership.
void Spiller::ensureSpillSlot(RegClass cls, SpillPlan &plan)
{
    if (plan.slot >= 0)
    {
        return;
    }
    plan.needsSpill = true;
    if (cls == RegClass::GPR)
    {
        plan.slot = nextSpillSlotGPR_++;
        return;
    }
    plan.slot = nextSpillSlotXMM_++;
}

/// @brief Emit a load instruction from a spill slot into a register.
/// @details The opcode chosen depends on the register class: general-purpose
///          registers use @c MOVrr while floating-point registers rely on
///          @c MOVSDmr.  The resulting instruction is ready to be inserted into
///          the MIR stream without additional operands.
/// @param cls Register class to load.
/// @param dst Physical register receiving the value.
/// @param plan Spill plan describing the source slot.
/// @return Machine instruction that reloads the spilled value.
MInstr Spiller::makeLoad(RegClass cls, PhysReg dst, const SpillPlan &plan) const
{
    if (cls == RegClass::GPR)
    {
        return MInstr::make(MOpcode::MOVrr,
                            {makePhysOperand(cls, dst), makeFrameOperand(plan.slot)});
    }
    return MInstr::make(MOpcode::MOVSDmr, {makePhysOperand(cls, dst), makeFrameOperand(plan.slot)});
}

/// @brief Emit a store instruction from a register into a spill slot.
/// @details Mirroring @ref makeLoad, the helper selects the appropriate opcode
///          for the register class and packages the operands so callers can
///          append the instruction directly to a prefix or suffix list.
/// @param cls Register class owning the value.
/// @param plan Spill plan containing the destination slot.
/// @param src Physical register providing the value to spill.
/// @return Machine instruction that writes the register to the stack slot.
MInstr Spiller::makeStore(RegClass cls, const SpillPlan &plan, PhysReg src) const
{
    if (cls == RegClass::GPR)
    {
        return MInstr::make(MOpcode::MOVrr,
                            {makeFrameOperand(plan.slot), makePhysOperand(cls, src)});
    }
    return MInstr::make(MOpcode::MOVSDrm, {makeFrameOperand(plan.slot), makePhysOperand(cls, src)});
}

/// @brief Materialise a spill for a live virtual register.
/// @details When the allocator runs out of free registers it calls into the
///          spiller to evict one active allocation.  The routine ensures a spill
///          slot exists, emits a store so the current value is preserved, returns
///          the physical register to the free pool, and updates bookkeeping so
///          future reloads know that the value resides in memory.
/// @param cls Register class of the spilled value.
/// @param vreg Virtual register identifier being spilled.
/// @param alloc Allocation record tracking the virtual register state.
/// @param pool Register pool receiving the freed physical register.
/// @param prefix Instruction buffer that receives the spill store.
/// @param result Global allocation result map updated to forget @p vreg.
void Spiller::spillValue(RegClass cls,
                         uint16_t vreg,
                         VirtualAllocation &alloc,
                         std::vector<PhysReg> &pool,
                         std::vector<MInstr> &prefix,
                         AllocationResult &result)
{
    ensureSpillSlot(cls, alloc.spill);
    prefix.push_back(makeStore(cls, alloc.spill, alloc.phys));
    pool.push_back(alloc.phys);
    alloc.hasPhys = false;
    alloc.spill.needsSpill = true;
    result.vregToPhys.erase(vreg);
}

/// @brief Create a memory operand referencing a spill slot.
/// @details Spill slots live at negative offsets from @c %rbp in units of eight
///          bytes.  The helper computes the byte displacement for @p slot and
///          returns a Machine operand that can be consumed by loads and stores.
/// @param slot Zero-based slot index to reference.
/// @return Memory operand pointing to the spill slot.
Operand Spiller::makeFrameOperand(int slot) const
{
    const auto base = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));
    const int32_t offset = -static_cast<int32_t>((slot + 1) * 8);
    return makeMemOperand(base, offset);
}

} // namespace viper::codegen::x64::ra
