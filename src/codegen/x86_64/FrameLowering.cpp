//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/codegen/x86_64/FrameLowering.cpp
// Purpose: Lower abstract frame information to concrete stack layouts for the
//          x86-64 backend, including spill slot assignment and prologue/epilogue
//          synthesis.
// Key invariants: Spill slots remain addressed relative to %rbp with 8-byte
//                 spacing, the outgoing argument area honours 16-byte alignment,
//                 and callee-saved registers are saved/restored in stack order.
// Ownership/Lifetime: Functions mutate Machine IR blocks in place using
//                     stack-allocated scratch structures; no persistent state is
//                     retained across invocations.
// Perf/Threading notes: Operates on one Machine IR function at a time and makes
//                       no assumptions about multi-threaded callers.
// Links: docs/architecture.md#codegen, docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "FrameLowering.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace viper::codegen::x64
{

namespace
{

constexpr int kSlotSizeBytes = 8;

/// @brief Uniquely identifies a spill slot by register class and index.
struct SlotKey
{
    RegClass cls{RegClass::GPR};
    int index{0};

    /// @brief Compare slot keys for equality.
    /// @details Spill slots match when both the register class and sequential index
    ///          align; this ensures GPR and XMM slots remain distinct even when their
    ///          indices coincide.
    bool operator==(const SlotKey &other) const noexcept
    {
        return cls == other.cls && index == other.index;
    }
};

struct SlotKeyHash
{
    /// @brief Hash a slot key for use in unordered containers.
    /// @details Combines the register class and index using a simple xor-based mix
    ///          that suffices for the small domain encountered during frame lowering.
    std::size_t operator()(const SlotKey &key) const noexcept
    {
        const auto clsVal = static_cast<std::size_t>(key.cls);
        const auto idxVal = static_cast<std::size_t>(key.index);
        return (idxVal << 1) ^ clsVal;
    }
};

/// @brief Round @p value up to the next multiple of @p align.
/// @details Used for stack size and outgoing argument area computations where the
///          SysV ABI mandates 16-byte alignment.  Assertions ensure callers supply
///          positive alignment values.
[[nodiscard]] int roundUp(int value, int align)
{
    assert(align > 0 && "alignment must be positive");
    const int remainder = value % align;
    if (remainder == 0)
    {
        return value;
    }
    return value + (align - remainder);
}

/// @brief Wrap a physical register enumerator in a Machine IR operand.
/// @details Keeps operand construction consistent when prologue/epilogue code needs
///          to reference callee-saved registers explicitly.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

/// @brief Produce a physical register operand suitable for memory bases.
/// @details Returns an @ref OpReg value referencing @p reg within the general-purpose
///          class so frame accesses can reuse the helper consistently.
[[nodiscard]] OpReg makePhysBase(PhysReg reg)
{
    return makePhysReg(RegClass::GPR, static_cast<uint16_t>(reg));
}

/// @brief Test whether @p reg is classified as callee-saved in the target info.
/// @details Searches both general-purpose and XMM callee-saved sets.  The result
///          steers whether @ref assignSpillSlots reserves stack space to preserve the
///          register across calls.
[[nodiscard]] bool isCalleeSaved(const TargetInfo &target, PhysReg reg)
{
    const auto hasReg = [&](const std::vector<PhysReg> &regs)
    { return std::find(regs.begin(), regs.end(), reg) != regs.end(); };
    return hasReg(target.calleeSavedGPR) || hasReg(target.calleeSavedXMM);
}

/// @brief Infer the register class that produced a memory operand.
/// @details When spill slots are represented by frame-indexed memory operands, the
///          lowering pass needs to know whether they store integer or floating-point
///          values.  The function inspects neighbouring operands to guess the class
///          and defaults to @ref RegClass::GPR when ambiguous.
[[nodiscard]] RegClass deduceMemClass(const MInstr &instr, std::size_t memIndex)
{
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx)
    {
        if (idx == memIndex)
        {
            continue;
        }
        if (const auto *reg = std::get_if<OpReg>(&instr.operands[idx]))
        {
            if (!reg->isPhys)
            {
                continue;
            }
            const auto phys = static_cast<PhysReg>(reg->idOrPhys);
            if (isXMM(phys))
            {
                return RegClass::XMM;
            }
            if (isGPR(phys))
            {
                return RegClass::GPR;
            }
        }
    }
    return RegClass::GPR;
}

/// @brief Compute the stack displacement for a callee-saved register spill slot.
/// @details Callee-saved registers live directly beneath the saved %rbp slot and are
///          stored in order.  Each slot consumes eight bytes, so the offset scales by
///          the slot index.
[[nodiscard]] int calleeSavedOffset(std::size_t index)
{
    return -static_cast<int>((index + 1) * kSlotSizeBytes);
}

} // namespace

/// @brief Assign concrete spill slot displacements for a Machine IR function.
/// @details Scans every instruction to detect placeholder offsets and converts them
///          into negative frame-relative displacements.  The routine also records
///          which callee-saved registers require saving, computes the final frame
///          size aligned to 16 bytes, and updates @p frame to describe the layout.
void assignSpillSlots(MFunction &func, const TargetInfo &target, FrameInfo &frame)
{
    std::unordered_set<PhysReg> usedCalleeSaved{};
    std::set<int> gprSlots{};
    std::set<int> xmmSlots{};

    for (auto &block : func.blocks)
    {
        for (auto &instr : block.instructions)
        {
            for (std::size_t idx = 0; idx < instr.operands.size(); ++idx)
            {
                auto &operand = instr.operands[idx];
                if (auto *reg = std::get_if<OpReg>(&operand); reg && reg->isPhys)
                {
                    const auto phys = static_cast<PhysReg>(reg->idOrPhys);
                    if (phys != PhysReg::RBP && phys != PhysReg::RSP && isCalleeSaved(target, phys))
                    {
                        usedCalleeSaved.insert(phys);
                    }
                }
                auto *mem = std::get_if<OpMem>(&operand);
                if (!mem)
                {
                    continue;
                }
                if (!mem->base.isPhys)
                {
                    continue;
                }
                const auto basePhys = static_cast<PhysReg>(mem->base.idOrPhys);
                if (basePhys != PhysReg::RBP)
                {
                    continue;
                }
                if (mem->disp >= 0)
                {
                    continue;
                }
                const int placeholder = -mem->disp;
                assert(placeholder % kSlotSizeBytes == 0 && placeholder > 0 &&
                       "spill slots expected to use 8-byte stepping");
                const int slotIndex = placeholder / kSlotSizeBytes - 1;
                const RegClass cls = deduceMemClass(instr, idx);
                if (cls == RegClass::XMM)
                {
                    xmmSlots.insert(slotIndex);
                }
                else
                {
                    gprSlots.insert(slotIndex);
                }
            }
        }
    }

    frame.usedCalleeSaved.clear();
    for (auto reg : target.calleeSavedGPR)
    {
        if (reg == PhysReg::RBP)
        {
            continue; // %rbp handled separately by the standard prologue/epilogue.
        }
        if (usedCalleeSaved.count(reg) != 0)
        {
            frame.usedCalleeSaved.push_back(reg);
        }
    }
    for (auto reg : target.calleeSavedXMM)
    {
        if (usedCalleeSaved.count(reg) != 0)
        {
            frame.usedCalleeSaved.push_back(reg);
        }
    }

    const int calleeSavedBytes = static_cast<int>(frame.usedCalleeSaved.size()) * kSlotSizeBytes;

    std::unordered_map<SlotKey, int, SlotKeyHash> slotOffsets{};
    int runningOffset = calleeSavedBytes;

    for (int slot : gprSlots)
    {
        runningOffset += kSlotSizeBytes;
        slotOffsets.emplace(SlotKey{RegClass::GPR, slot}, -runningOffset);
    }
    for (int slot : xmmSlots)
    {
        runningOffset += kSlotSizeBytes;
        slotOffsets.emplace(SlotKey{RegClass::XMM, slot}, -runningOffset);
    }

    frame.spillAreaGPR = static_cast<int>(gprSlots.size()) * kSlotSizeBytes;
    frame.spillAreaXMM = static_cast<int>(xmmSlots.size()) * kSlotSizeBytes;

    if (frame.outgoingArgArea < 0)
    {
        frame.outgoingArgArea = 0;
    }
    frame.outgoingArgArea = roundUp(frame.outgoingArgArea, 16);

    const int rawFrameSize = runningOffset + frame.outgoingArgArea;
    frame.frameSize = roundUp(rawFrameSize, 16);

    for (auto &block : func.blocks)
    {
        for (auto &instr : block.instructions)
        {
            for (std::size_t idx = 0; idx < instr.operands.size(); ++idx)
            {
                auto *mem = std::get_if<OpMem>(&instr.operands[idx]);
                if (!mem)
                {
                    continue;
                }
                if (!mem->base.isPhys)
                {
                    continue;
                }
                const auto basePhys = static_cast<PhysReg>(mem->base.idOrPhys);
                if (basePhys != PhysReg::RBP)
                {
                    continue;
                }
                if (mem->disp >= 0)
                {
                    continue;
                }
                const int placeholder = -mem->disp;
                if (placeholder % kSlotSizeBytes != 0 || placeholder <= 0)
                {
                    continue;
                }
                const int slotIndex = placeholder / kSlotSizeBytes - 1;
                const RegClass cls = deduceMemClass(instr, idx);
                const SlotKey key{cls, slotIndex};
                auto it = slotOffsets.find(key);
                if (it != slotOffsets.end())
                {
                    mem->disp = it->second;
                }
            }
        }
    }
}

/// @brief Insert prologue and epilogue sequences using the computed frame layout.
/// @details Prepends instructions to the entry block that establish %rbp, adjust
///          %rsp, and spill any callee-saved registers recorded in @p frame.  Each
///          return instruction in the function receives a mirrored epilogue that
///          restores the saved registers, resets %rsp, and reloads %rbp before
///          issuing @c RET.
void insertPrologueEpilogue(MFunction &func, const TargetInfo &target, const FrameInfo &frame)
{
    if (func.blocks.empty())
    {
        return;
    }

    (void)target;

    const auto rspOperand = makePhysOperand(RegClass::GPR, PhysReg::RSP);
    const auto rbpOperand = makePhysOperand(RegClass::GPR, PhysReg::RBP);
    const auto rspBase = makePhysBase(PhysReg::RSP);
    const auto rbpBase = makePhysBase(PhysReg::RBP);

    std::vector<MInstr> prologue{};
    prologue.reserve(4 + frame.usedCalleeSaved.size());

    prologue.push_back(MInstr::make(MOpcode::ADDri, {rspOperand, makeImmOperand(-kSlotSizeBytes)}));
    prologue.push_back(MInstr::make(MOpcode::MOVrr, {makeMemOperand(rspBase, 0), rbpOperand}));
    prologue.push_back(MInstr::make(MOpcode::MOVrr, {rbpOperand, rspOperand}));

    if (frame.frameSize > 0)
    {
        prologue.push_back(
            MInstr::make(MOpcode::ADDri, {rspOperand, makeImmOperand(-frame.frameSize)}));
    }

    for (std::size_t idx = 0; idx < frame.usedCalleeSaved.size(); ++idx)
    {
        const auto reg = frame.usedCalleeSaved[idx];
        assert(isGPR(reg) && "Phase A expects only GPR callee-saved registers");
        const int offset = calleeSavedOffset(idx);
        prologue.push_back(
            MInstr::make(MOpcode::MOVrr,
                         {makeMemOperand(rbpBase, offset), makePhysOperand(RegClass::GPR, reg)}));
    }

    auto &entry = func.blocks.front();
    std::vector<MInstr> updatedEntry{};
    updatedEntry.reserve(prologue.size() + entry.instructions.size());
    updatedEntry.insert(updatedEntry.end(), prologue.begin(), prologue.end());
    updatedEntry.insert(updatedEntry.end(), entry.instructions.begin(), entry.instructions.end());
    entry.instructions = std::move(updatedEntry);

    std::vector<MInstr> epilogue{};
    epilogue.reserve(3 + frame.usedCalleeSaved.size());

    for (std::size_t idx = frame.usedCalleeSaved.size(); idx > 0; --idx)
    {
        const auto reg = frame.usedCalleeSaved[idx - 1];
        assert(isGPR(reg) && "Phase A expects only GPR callee-saved registers");
        const int offset = calleeSavedOffset(idx - 1);
        epilogue.push_back(
            MInstr::make(MOpcode::MOVrr,
                         {makePhysOperand(RegClass::GPR, reg), makeMemOperand(rbpBase, offset)}));
    }

    epilogue.push_back(MInstr::make(MOpcode::MOVrr, {rspOperand, rbpOperand}));
    epilogue.push_back(MInstr::make(MOpcode::MOVrr, {rbpOperand, makeMemOperand(rspBase, 0)}));
    epilogue.push_back(MInstr::make(MOpcode::ADDri, {rspOperand, makeImmOperand(kSlotSizeBytes)}));

    for (auto &block : func.blocks)
    {
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx)
        {
            if (block.instructions[idx].opcode == MOpcode::RET)
            {
                block.instructions.insert(block.instructions.begin() +
                                              static_cast<std::ptrdiff_t>(idx),
                                          epilogue.begin(),
                                          epilogue.end());
                idx += epilogue.size();
            }
        }
    }
}

} // namespace viper::codegen::x64
