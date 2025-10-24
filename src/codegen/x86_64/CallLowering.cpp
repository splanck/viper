// src/codegen/x86_64/CallLowering.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Provide the lowering logic that maps abstract call descriptions to
//          concrete Machine IR conforming to the SysV AMD64 ABI.
// Invariants: Argument registers are populated in the canonical ABI order and
//             stack arguments are stored in 8-byte slots within the caller's
//             outgoing argument area. Emitted instruction order matches the
//             plan-provided argument order.
// Ownership: Routines mutate Machine IR blocks in-place and update the provided
//            frame summary without assuming ownership of any operand storage.
// Notes: Translation unit depends on CallLowering.hpp, FrameLowering.hpp, and
//        the C++ standard library only.

#include "CallLowering.hpp"

#include "FrameLowering.hpp"

#include <algorithm>
#include <cassert>

namespace viper::codegen::x64
{

namespace
{

constexpr std::size_t kGprArgLimit = 6;
constexpr std::size_t kXmmArgLimit = 8;
constexpr std::size_t kSlotSizeBytes = 8;
constexpr PhysReg kScratchGPR = PhysReg::R11;
constexpr PhysReg kScratchXMM = PhysReg::XMM15;

[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

[[nodiscard]] OpReg makePhysBase(PhysReg reg)
{
    return makePhysReg(RegClass::GPR, static_cast<uint16_t>(reg));
}

[[nodiscard]] bool isBoolProducer(const MInstr &instr, uint16_t vreg)
{
    if (instr.operands.empty())
    {
        return false;
    }
    if (const auto *reg = std::get_if<OpReg>(&instr.operands.front()); reg)
    {
        if (!reg->isPhys && reg->idOrPhys == vreg)
        {
            return instr.opcode == MOpcode::SETcc;
        }
    }
    return false;
}

[[nodiscard]] bool isI1Value(const MBasicBlock &block, std::size_t searchLimit, uint16_t vreg)
{
    if (searchLimit > block.instructions.size())
    {
        searchLimit = block.instructions.size();
    }
    while (searchLimit > 0)
    {
        --searchLimit;
        const auto &instr = block.instructions[searchLimit];
        if (isBoolProducer(instr, vreg))
        {
            return true;
        }
        if (!instr.operands.empty())
        {
            if (const auto *reg = std::get_if<OpReg>(&instr.operands.front()); reg)
            {
                if (!reg->isPhys && reg->idOrPhys == vreg)
                {
                    return false;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] Operand makeStackSlot(int32_t offset)
{
    return makeMemOperand(makePhysBase(PhysReg::RSP), offset);
}

[[nodiscard]] std::size_t alignToSlot(std::size_t bytes)
{
    if (bytes % kSlotSizeBytes == 0)
    {
        return bytes;
    }
    return bytes + (kSlotSizeBytes - (bytes % kSlotSizeBytes));
}

} // namespace

void lowerCall(MBasicBlock &block,
               std::size_t insertIdx,
               const CallLoweringPlan &plan,
               const TargetInfo &target,
               FrameInfo &frame)
{
    assert(insertIdx <= block.instructions.size() && "insert index out of range");

    auto insertIt = block.instructions.begin();
    std::advance(insertIt, static_cast<std::ptrdiff_t>(insertIdx));

    auto insertInstr = [&](MInstr instr)
    {
        insertIt = block.instructions.insert(insertIt, std::move(instr));
        ++insertIt;
    };

    std::size_t gprUsed = 0;
    std::size_t xmmUsed = 0;
    std::size_t stackBytes = 0;

    for (const auto &arg : plan.args)
    {
        const auto currentIdx =
            static_cast<std::size_t>(std::distance(block.instructions.begin(), insertIt));

        switch (arg.kind)
        {
            case CallArg::GPR:
            {
                if (gprUsed < kGprArgLimit)
                {
                    const PhysReg destReg = target.intArgOrder[gprUsed++];
                    if (arg.isImm)
                    {
                        insertInstr(MInstr::make(
                            MOpcode::MOVri,
                            {makePhysOperand(RegClass::GPR, destReg), makeImmOperand(arg.imm)}));
                    }
                    else
                    {
                        const Operand src = makeVRegOperand(RegClass::GPR, arg.vreg);
                        if (isI1Value(block, currentIdx, arg.vreg))
                        {
                            insertInstr(
                                MInstr::make(MOpcode::MOVZXrr32,
                                             {makePhysOperand(RegClass::GPR, destReg), src}));
                        }
                        else
                        {
                            insertInstr(MInstr::make(
                                MOpcode::MOVrr, {makePhysOperand(RegClass::GPR, destReg), src}));
                        }
                    }
                }
                else
                {
                    const auto slotOffset = static_cast<int32_t>(stackBytes);
                    stackBytes += kSlotSizeBytes;
                    const Operand dest = makeStackSlot(slotOffset);
                    if (arg.isImm)
                    {
                        const Operand scratch = makePhysOperand(RegClass::GPR, kScratchGPR);
                        insertInstr(
                            MInstr::make(MOpcode::MOVri, {scratch, makeImmOperand(arg.imm)}));
                        insertInstr(MInstr::make(MOpcode::MOVrr, {dest, scratch}));
                    }
                    else if (isI1Value(block, currentIdx, arg.vreg))
                    {
                        const Operand scratch = makePhysOperand(RegClass::GPR, kScratchGPR);
                        insertInstr(
                            MInstr::make(MOpcode::MOVZXrr32,
                                         {scratch, makeVRegOperand(RegClass::GPR, arg.vreg)}));
                        insertInstr(MInstr::make(MOpcode::MOVrr, {dest, scratch}));
                    }
                    else
                    {
                        insertInstr(MInstr::make(MOpcode::MOVrr,
                                                 {dest, makeVRegOperand(RegClass::GPR, arg.vreg)}));
                    }
                }
                break;
            }
            case CallArg::XMM:
            {
                if (xmmUsed < kXmmArgLimit)
                {
                    const PhysReg destReg = target.f64ArgOrder[xmmUsed++];
                    if (arg.isImm)
                    {
                        // Phase A backend does not yet support immediate materialisation into XMM
                        // args.
                        const Operand scratchGpr = makePhysOperand(RegClass::GPR, kScratchGPR);
                        insertInstr(
                            MInstr::make(MOpcode::MOVri, {scratchGpr, makeImmOperand(arg.imm)}));
                        insertInstr(
                            MInstr::make(MOpcode::CVTSI2SD,
                                         {makePhysOperand(RegClass::XMM, destReg), scratchGpr}));
                    }
                    else
                    {
                        insertInstr(MInstr::make(MOpcode::MOVSDrr,
                                                 {makePhysOperand(RegClass::XMM, destReg),
                                                  makeVRegOperand(RegClass::XMM, arg.vreg)}));
                    }
                }
                else
                {
                    const auto slotOffset = static_cast<int32_t>(stackBytes);
                    stackBytes += kSlotSizeBytes;
                    const Operand dest = makeStackSlot(slotOffset);
                    if (arg.isImm)
                    {
                        const Operand scratchGpr = makePhysOperand(RegClass::GPR, kScratchGPR);
                        const Operand scratchXmm = makePhysOperand(RegClass::XMM, kScratchXMM);
                        insertInstr(
                            MInstr::make(MOpcode::MOVri, {scratchGpr, makeImmOperand(arg.imm)}));
                        insertInstr(MInstr::make(MOpcode::CVTSI2SD, {scratchXmm, scratchGpr}));
                        insertInstr(MInstr::make(MOpcode::MOVSDrm, {dest, scratchXmm}));
                    }
                    else
                    {
                        insertInstr(MInstr::make(MOpcode::MOVSDrm,
                                                 {dest, makeVRegOperand(RegClass::XMM, arg.vreg)}));
                    }
                }
                break;
            }
        }
    }

    frame.outgoingArgArea =
        std::max(frame.outgoingArgArea, static_cast<int>(alignToSlot(stackBytes)));

    insertInstr(MInstr::make(MOpcode::CALL, {makeLabelOperand(plan.calleeLabel)}));
}

} // namespace viper::codegen::x64
