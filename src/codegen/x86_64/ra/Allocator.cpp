//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

namespace viper::codegen::x64::ra
{

namespace
{

using RegPool = std::vector<PhysReg>;

template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};
template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

[[nodiscard]] bool isReservedGPR(PhysReg reg) noexcept
{
    return reg == PhysReg::RSP || reg == PhysReg::RBP;
}

[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

} // namespace

LinearScanAllocator::LinearScanAllocator(MFunction &func,
                                         const TargetInfo &target,
                                         const LiveIntervals &intervals)
    : func_(func), target_(target), intervals_(intervals)
{
    buildPools();
}

AllocationResult LinearScanAllocator::run()
{
    Coalescer coalescer{*this, spiller_};
    for (auto &block : func_.blocks)
    {
        processBlock(block, coalescer);
        releaseActiveForBlock();
    }
    result_.spillSlotsGPR = spiller_.gprSlots();
    result_.spillSlotsXMM = spiller_.xmmSlots();
    return result_;
}

void LinearScanAllocator::buildPools()
{
    auto appendRegs = [](RegPool &pool, const std::vector<PhysReg> &regs)
    {
        pool.insert(pool.end(), regs.begin(), regs.end());
    };

    appendRegs(freeGPR_, target_.callerSavedGPR);
    appendRegs(freeGPR_, target_.calleeSavedGPR);
    freeGPR_.erase(std::remove_if(freeGPR_.begin(),
                                  freeGPR_.end(),
                                  [](PhysReg reg) { return isReservedGPR(reg); }),
                   freeGPR_.end());

    appendRegs(freeXMM_, target_.callerSavedXMM);
    appendRegs(freeXMM_, target_.calleeSavedXMM);
}

std::vector<PhysReg> &LinearScanAllocator::poolFor(RegClass cls)
{
    return cls == RegClass::GPR ? freeGPR_ : freeXMM_;
}

std::vector<uint16_t> &LinearScanAllocator::activeFor(RegClass cls)
{
    return cls == RegClass::GPR ? activeGPR_ : activeXMM_;
}

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

void LinearScanAllocator::addActive(RegClass cls, uint16_t id)
{
    auto &active = activeFor(cls);
    if (std::find(active.begin(), active.end(), id) == active.end())
    {
        active.push_back(id);
    }
}

void LinearScanAllocator::removeActive(RegClass cls, uint16_t id)
{
    auto &active = activeFor(cls);
    active.erase(std::remove(active.begin(), active.end(), id), active.end());
}

PhysReg LinearScanAllocator::takeRegister(RegClass cls, std::vector<MInstr> &prefix)
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

void LinearScanAllocator::releaseRegister(PhysReg phys, RegClass cls)
{
    poolFor(cls).push_back(phys);
}

void LinearScanAllocator::spillOne(RegClass cls, std::vector<MInstr> &prefix)
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
    spiller_.spillValue(cls, victimId, victim, poolFor(cls), prefix, result_);
}

void LinearScanAllocator::processBlock(MBasicBlock &block, Coalescer &coalescer)
{
    std::vector<MInstr> rewritten{};
    rewritten.reserve(block.instructions.size());

    for (const auto &instr : block.instructions)
    {
        if (instr.opcode == MOpcode::PX_COPY)
        {
            coalescer.lower(instr, rewritten);
            continue;
        }

        std::vector<MInstr> prefix{};
        std::vector<MInstr> suffix{};
        std::vector<ScratchRelease> scratch{};
        MInstr current = instr;
        auto roles = classifyOperands(current);

        for (std::size_t idx = 0; idx < current.operands.size(); ++idx)
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

void LinearScanAllocator::releaseActiveForBlock()
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

std::vector<LinearScanAllocator::OperandRole>
LinearScanAllocator::classifyOperands(const MInstr &instr) const
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

void LinearScanAllocator::handleOperand(Operand &operand,
                                        const OperandRole &role,
                                        std::vector<MInstr> &prefix,
                                        std::vector<MInstr> &suffix,
                                        std::vector<ScratchRelease> &scratch)
{
    std::visit(
        Overload{[&](OpReg &reg) { processRegOperand(reg, role, prefix, suffix, scratch); },
                 [&](OpMem &mem)
                 {
                     OperandRole baseRole{true, false};
                     processRegOperand(mem.base, baseRole, prefix, suffix, scratch);
                 },
                 [](auto &) {}},
        operand);
}

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

MInstr LinearScanAllocator::makeMove(RegClass cls, PhysReg dst, PhysReg src) const
{
    if (cls == RegClass::GPR)
    {
        return MInstr::make(MOpcode::MOVrr,
                            {makePhysOperand(cls, dst), makePhysOperand(cls, src)});
    }
    return MInstr::make(MOpcode::MOVSDrr,
                        {makePhysOperand(cls, dst), makePhysOperand(cls, src)});
}

} // namespace viper::codegen::x64::ra
