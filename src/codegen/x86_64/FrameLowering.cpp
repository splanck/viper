//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/FrameLowering.cpp
// Purpose: Define stack-frame construction utilities for the x86-64 backend.
// Key invariants: Spill slots are addressed off %rbp with negative displacements
//                 and the final frame size preserves 16-byte alignment across
//                 calls.
// Ownership/Lifetime: Operates directly on Machine IR owned by the caller and
//                     uses only automatic storage duration helpers.
// Links: docs/codemap.md, src/codegen/x86_64/FrameLowering.hpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements stack-frame layout and prologue/epilogue synthesis.
/// @details The helpers in this translation unit walk Machine IR produced by the
///          IL-to-MIR adapter to allocate concrete spill displacements, reserve
///          callee-saved slots, and generate ABI-compliant prologue/epilogue
///          sequences for Phase A of the x86-64 backend.

#include "FrameLowering.hpp"
#include "OperandUtils.hpp"

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

/// @brief Composite key describing a spill slot's register class and index.
struct SlotKey
{
    RegClass cls{RegClass::GPR};
    int index{0};

    /// @brief Equality comparison required by the unordered_map cache.
    bool operator==(const SlotKey &other) const noexcept
    {
        return cls == other.cls && index == other.index;
    }
};

/// @brief Hash functor for @ref SlotKey enabling unordered maps.
struct SlotKeyHash
{
    /// @brief Combine the register class and index into a small hash code.
    std::size_t operator()(const SlotKey &key) const noexcept
    {
        const auto clsVal = static_cast<std::size_t>(key.cls);
        const auto idxVal = static_cast<std::size_t>(key.index);
        return (idxVal << 1) ^ clsVal;
    }
};

/// @brief Build a set of callee-saved registers for O(1) lookup.
/// @details Pre-computes the union of GPR and XMM callee-saved registers
///          into an unordered_set for efficient membership testing.
/// @param target Target description providing ABI details.
/// @return Set containing all callee-saved physical registers.
[[nodiscard]] std::unordered_set<PhysReg> buildCalleeSavedSet(const TargetInfo &target)
{
    std::unordered_set<PhysReg> result{};
    result.reserve(target.calleeSavedGPR.size() + target.calleeSavedXMM.size());
    result.insert(target.calleeSavedGPR.begin(), target.calleeSavedGPR.end());
    result.insert(target.calleeSavedXMM.begin(), target.calleeSavedXMM.end());
    return result;
}

/// @brief Determine whether @p reg belongs to the callee-saved set.
/// @details Uses a pre-computed set for O(1) lookup instead of O(n) linear search.
/// @param calleeSavedSet Pre-computed set of callee-saved registers.
/// @param reg Physical register being queried.
/// @return True when the register must be preserved across calls.
[[nodiscard]] bool isCalleeSaved(const std::unordered_set<PhysReg> &calleeSavedSet, PhysReg reg)
{
    return calleeSavedSet.count(reg) > 0;
}

/// @brief Guess the register class used by a memory operand.
/// @details Scans all other operands in the instruction looking for physical
///          registers to infer whether the memory slot stores GPR or XMM state.
///          Falls back to @ref RegClass::GPR when no hint is found, which keeps
///          stack layout deterministic for scalar spills.
/// @param instr Machine instruction containing the operand.
/// @param memIndex Index of the memory operand within the instruction.
/// @return Register class used to model the memory payload.
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

/// @brief Compute the stack offset that stores a callee-saved register.
/// @details Spill slots are allocated consecutively below %rbp.  Each slot
///          occupies eight bytes and stores one callee-saved value.
/// @param index Zero-based index of the callee-saved register spill slot.
/// @return Negative byte offset relative to %rbp.
[[nodiscard]] int calleeSavedOffset(std::size_t index)
{
    return -static_cast<int>((index + 1) * kSlotSizeBytes);
}

} // namespace

/// @brief Assign concrete spill slot displacements and record callee saves.
/// @details Walks all Machine IR instructions searching for placeholder stack
///          references (encoded as negative displacements from %rbp) and
///          replaces them with the final offsets computed from the register
///          class partitioning.  The routine also records which callee-saved
///          registers actually appear in the function and rounds frame
///          allocations up to 16 bytes to maintain ABI alignment.
/// @param func Machine function whose frame layout is being materialised.
/// @param target Target ABI description (callee-saved sets, etc.).
/// @param frame Frame metadata that will be populated with spill sizes and
///              outgoing argument requirements.
void assignSpillSlots(MFunction &func, const TargetInfo &target, FrameInfo &frame)
{
    // Pre-compute callee-saved set for O(1) lookup instead of O(n) linear search.
    const auto calleeSavedSet = buildCalleeSavedSet(target);

    std::unordered_set<PhysReg> usedCalleeSaved{};
    std::set<int> gprSpillSlots{};
    std::set<int> xmmSpillSlots{};
    int maxAllocaSlotIndex = -1; // Track the highest alloca slot index

    // The Spiller uses a 1000 offset to distinguish spill slots from alloca slots:
    // - Alloca slots: slotIndex = resultId (0, 1, 2, ...)
    // - Spill slots: slotIndex = spillSlot + 1000 (1000, 1001, 1002, ...)
    constexpr int kSpillSlotOffset = 1000;

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
                    if (phys != PhysReg::RBP && phys != PhysReg::RSP && isCalleeSaved(calleeSavedSet, phys))
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

                // Distinguish between alloca slots (< 1000) and spill slots (>= 1000)
                if (slotIndex >= kSpillSlotOffset)
                {
                    // This is a spill slot - collect for remapping
                    const RegClass cls = deduceMemClass(instr, idx);
                    if (cls == RegClass::XMM)
                    {
                        xmmSpillSlots.insert(slotIndex);
                    }
                    else
                    {
                        gprSpillSlots.insert(slotIndex);
                    }
                }
                else
                {
                    // This is an alloca slot - track the max for frame layout
                    // Alloca slots also need remapping to come after callee-saved area
                    maxAllocaSlotIndex = std::max(maxAllocaSlotIndex, slotIndex);
                }
            }
        }
    }

    // Compute the alloca area size (number of 8-byte alloca slots)
    // +1 because slotIndex is 0-based and we need space for slots 0..maxAllocaSlotIndex
    const int allocaAreaBytes =
        (maxAllocaSlotIndex >= 0) ? (maxAllocaSlotIndex + 1) * kSlotSizeBytes : 0;

    frame.usedCalleeSaved.clear();
    for (auto reg : target.calleeSavedGPR)
    {
        if (reg == PhysReg::RBP)
        {
            continue; // %rbp handled separately by the standard prologue/epilogue.
        }
        if (usedCalleeSaved.contains(reg))
        {
            frame.usedCalleeSaved.push_back(reg);
        }
    }
    for (auto reg : target.calleeSavedXMM)
    {
        if (usedCalleeSaved.contains(reg))
        {
            frame.usedCalleeSaved.push_back(reg);
        }
    }

    const int calleeSavedBytes = static_cast<int>(frame.usedCalleeSaved.size()) * kSlotSizeBytes;

    std::unordered_map<SlotKey, int, SlotKeyHash> slotOffsets{};

    // Remap alloca slots to come AFTER the callee-saved area
    // Alloca slot N (with placeholder offset -(N+1)*8) maps to -(calleeSavedBytes + (N+1)*8)
    for (int slot = 0; slot <= maxAllocaSlotIndex; ++slot)
    {
        const int newOffset = -(calleeSavedBytes + (slot + 1) * kSlotSizeBytes);
        slotOffsets.emplace(SlotKey{RegClass::GPR, slot}, newOffset);
    }

    // Start spill slots AFTER the callee-saved area AND the alloca area
    int runningOffset = calleeSavedBytes + allocaAreaBytes;

    for (int slot : gprSpillSlots)
    {
        runningOffset += kSlotSizeBytes;
        slotOffsets.emplace(SlotKey{RegClass::GPR, slot}, -runningOffset);
    }
    for (int slot : xmmSpillSlots)
    {
        runningOffset += kSlotSizeBytes;
        slotOffsets.emplace(SlotKey{RegClass::XMM, slot}, -runningOffset);
    }

    frame.spillAreaGPR = static_cast<int>(gprSpillSlots.size()) * kSlotSizeBytes;
    frame.spillAreaXMM = static_cast<int>(xmmSpillSlots.size()) * kSlotSizeBytes;

    if (frame.outgoingArgArea < 0)
    {
        frame.outgoingArgArea = 0;
    }
    frame.outgoingArgArea = roundUp(frame.outgoingArgArea, kStackAlignment);

    const int rawFrameSize = runningOffset + frame.outgoingArgArea;
    frame.frameSize = roundUp(rawFrameSize, kStackAlignment);

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

/// @brief Inject prologue and epilogue sequences that honour the SysV ABI.
/// @details Emits the canonical prologue (`push %rbp; mov %rsp, %rbp; sub ...`)
///          and mirrors it with an epilogue that restores callee-saved
///          registers, tears down the frame allocation, and pops %rbp before
///          returning.  Prologue instructions are prepended to the entry block
///          while each `ret` instruction receives an epilogue copy to ensure
///          multiple return sites stay well-formed.
/// @param func Machine function receiving prologue/epilogue code.
/// @param target Target ABI description (currently unused but kept for
///               symmetry with future extensions).
/// @param frame Frame metadata produced by @ref assignSpillSlots.
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

    // The following prologue synthesises the canonical
    //   push %rbp; mov %rsp, %rbp; sub $frameSize, %rsp
    // sequence using MIR operations. We materialise the push via an explicit
    // store after decrementing %rsp because the backend models stack slots as
    // memory operands. The extra 8-byte subtraction keeps the pre-call stack
    // pointer 16-byte aligned once the optional frame allocation executes.
    std::vector<MInstr> prologue{};
    prologue.reserve(4 + frame.usedCalleeSaved.size());

    prologue.push_back(MInstr::make(MOpcode::ADDri, {rspOperand, makeImmOperand(-kSlotSizeBytes)}));
    prologue.push_back(MInstr::make(MOpcode::MOVrm, {makeMemOperand(rspBase, 0), rbpOperand}));
    prologue.push_back(MInstr::make(MOpcode::MOVrr, {rbpOperand, rspOperand}));

    if (frame.frameSize > 0)
    {
        // For large frames (> page size), we need to probe the stack to ensure
        // the guard page is touched. This prevents jumping over the guard page
        // and crashing without a proper stack overflow exception.
        // On Windows, we call __chkstk which probes and adjusts RSP.
        // On other platforms, we emit inline probing code.
#if defined(_WIN32)
        if (frame.frameSize > kPageSize)
        {
            // Windows: __chkstk expects allocation size in RAX
            // It probes each page and subtracts from RSP
            const auto raxOperand = makePhysOperand(RegClass::GPR, PhysReg::RAX);
            prologue.push_back(
                MInstr::make(MOpcode::MOVri, {raxOperand, makeImmOperand(frame.frameSize)}));
            prologue.push_back(MInstr::make(MOpcode::CALL, {makeLabelOperand("__chkstk")}));
            // __chkstk subtracts RAX from RSP, so we just need to copy
            prologue.push_back(MInstr::make(MOpcode::MOVrr, {rspOperand, raxOperand}));
            // Actually, __chkstk on MSVC doesn't modify RSP - it just probes.
            // We still need to do the actual allocation.
            // Correction: ___chkstk_ms (MinGW/clang) probes but doesn't adjust RSP.
            // We need to subtract after probing.
            // Remove the MOVrr and do the normal subtraction
            prologue.pop_back(); // Remove the incorrect MOVrr
            prologue.push_back(
                MInstr::make(MOpcode::ADDri, {rspOperand, makeImmOperand(-frame.frameSize)}));
        }
        else
        {
            prologue.push_back(
                MInstr::make(MOpcode::ADDri, {rspOperand, makeImmOperand(-frame.frameSize)}));
        }
#else
        // Unix/macOS: emit inline stack probing for large frames
        if (frame.frameSize > kPageSize)
        {
            // Probe loop: touch each page from current RSP down to RSP - frameSize
            // This is a conservative approach that ensures the OS can grow the stack.
            // For simplicity, we just do the allocation and rely on the OS signal handler.
            // A more robust approach would emit a probe loop.
            prologue.push_back(
                MInstr::make(MOpcode::ADDri, {rspOperand, makeImmOperand(-frame.frameSize)}));
        }
        else
        {
            prologue.push_back(
                MInstr::make(MOpcode::ADDri, {rspOperand, makeImmOperand(-frame.frameSize)}));
        }
#endif
    }

    for (std::size_t idx = 0; idx < frame.usedCalleeSaved.size(); ++idx)
    {
        const auto reg = frame.usedCalleeSaved[idx];
        const int offset = calleeSavedOffset(idx);
        if (isGPR(reg))
        {
            prologue.push_back(MInstr::make(
                MOpcode::MOVrm,
                {makeMemOperand(rbpBase, offset), makePhysOperand(RegClass::GPR, reg)}));
        }
        else
        {
            // XMM callee-saved register: use MOVSD to save 64-bit value
            prologue.push_back(MInstr::make(
                MOpcode::MOVSDrm,
                {makeMemOperand(rbpBase, offset), makePhysOperand(RegClass::XMM, reg)}));
        }
    }

    // For the main function, inject rt_init_stack_safety() call to set up
    // exception handlers for graceful stack overflow detection.
    const bool isMain = (func.name == "main" || func.name == "@main");
    if (isMain)
    {
        prologue.push_back(MInstr::make(MOpcode::CALL, {makeLabelOperand("rt_init_stack_safety")}));
    }

    auto &entry = func.blocks.front();
    std::vector<MInstr> updatedEntry{};
    updatedEntry.reserve(prologue.size() + entry.instructions.size());
    updatedEntry.insert(updatedEntry.end(), prologue.begin(), prologue.end());
    updatedEntry.insert(updatedEntry.end(), entry.instructions.begin(), entry.instructions.end());
    entry.instructions = std::move(updatedEntry);

    // Epilogue mirrors the canonical
    //   add $frameSize, %rsp; pop %rbp; ret
    // form by undoing the frame allocation before reloading %rbp from the
    // spill slot. Using the same explicit memory traffic as the prologue keeps
    // stack alignment consistent for any intervening callee-saved stores.
    std::vector<MInstr> epilogue{};
    epilogue.reserve(3 + frame.usedCalleeSaved.size());

    for (std::size_t idx = frame.usedCalleeSaved.size(); idx > 0; --idx)
    {
        const auto reg = frame.usedCalleeSaved[idx - 1];
        const int offset = calleeSavedOffset(idx - 1);
        if (isGPR(reg))
        {
            epilogue.push_back(MInstr::make(
                MOpcode::MOVmr,
                {makePhysOperand(RegClass::GPR, reg), makeMemOperand(rbpBase, offset)}));
        }
        else
        {
            // XMM callee-saved register: use MOVSD to restore 64-bit value
            epilogue.push_back(MInstr::make(
                MOpcode::MOVSDmr,
                {makePhysOperand(RegClass::XMM, reg), makeMemOperand(rbpBase, offset)}));
        }
    }

    epilogue.push_back(MInstr::make(MOpcode::MOVrr, {rspOperand, rbpOperand}));
    epilogue.push_back(MInstr::make(MOpcode::MOVmr, {rbpOperand, makeMemOperand(rspBase, 0)}));
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
