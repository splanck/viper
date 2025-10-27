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

namespace viper::codegen::x64::ra
{

namespace
{

[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

} // namespace

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

MInstr Spiller::makeLoad(RegClass cls, PhysReg dst, const SpillPlan &plan) const
{
    if (cls == RegClass::GPR)
    {
        return MInstr::make(MOpcode::MOVrr, {makePhysOperand(cls, dst), makeFrameOperand(plan.slot)});
    }
    return MInstr::make(MOpcode::MOVSDmr, {makePhysOperand(cls, dst), makeFrameOperand(plan.slot)});
}

MInstr Spiller::makeStore(RegClass cls, const SpillPlan &plan, PhysReg src) const
{
    if (cls == RegClass::GPR)
    {
        return MInstr::make(MOpcode::MOVrr, {makeFrameOperand(plan.slot), makePhysOperand(cls, src)});
    }
    return MInstr::make(MOpcode::MOVSDrm, {makeFrameOperand(plan.slot), makePhysOperand(cls, src)});
}

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

Operand Spiller::makeFrameOperand(int slot) const
{
    const auto base = makePhysReg(RegClass::GPR, static_cast<uint16_t>(PhysReg::RBP));
    const int32_t offset = -static_cast<int32_t>((slot + 1) * 8);
    return makeMemOperand(base, offset);
}

} // namespace viper::codegen::x64::ra
