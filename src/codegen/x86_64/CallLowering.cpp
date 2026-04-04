//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the call-lowering phase for the x86-64 backend.  The translation
// unit maps abstract call plans into concrete Machine IR that abides by the
// SysV AMD64 ABI, ensuring registers and stack slots are populated in the
// required order while updating frame metadata so later passes can reserve
// stack space correctly.
//
// The lowering logic operates directly on the caller's Machine IR, threading in
// scratch registers when values must be moved through temporaries and aligning
// outgoing argument areas to eight-byte boundaries.  Plans produced by
// @ref CallLoweringPlan guide the transformation so the implementation stays
// decoupled from IL-level call semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief x86-64 call lowering routines that translate abstract plans into MIR.
/// @details Provides helpers that materialise physical register operands,
///          detect SSA values that already carry boolean semantics, and arrange
///          stack arguments for calls.  The main @ref lowerCall entry point is
///          invoked by the backend once per call site.

#include "CallLowering.hpp"

#include "FrameLowering.hpp"
#include "OperandUtils.hpp"
#include "TargetX64.hpp"

#include <algorithm>
#include <cassert>
#include <string>

namespace viper::codegen::x64 {

namespace {

constexpr PhysReg kScratchGPR = PhysReg::R11;
constexpr PhysReg kScratchXMM = PhysReg::XMM15;

/// @brief Decide whether an instruction produces the boolean SSA value @p vreg.
///
/// @details Walks the instruction's leading operand to see if it writes the
///          provided virtual register via a @c SETcc opcode.  This mirrors the
///          code generator's convention where boolean producers funnel through
///          @c SETcc before materialisation.
///
/// @param instr Instruction whose operands are inspected.
/// @param vreg Virtual register identifier associated with the SSA value.
/// @return True when the instruction writes @p vreg as a boolean result.
[[nodiscard]] bool isBoolProducer(const MInstr &instr, uint16_t vreg) {
    if (instr.operands.empty()) {
        return false;
    }
    if (const auto *reg = std::get_if<OpReg>(&instr.operands.front()); reg) {
        if (!reg->isPhys && reg->idOrPhys == vreg) {
            return instr.opcode == MOpcode::SETcc;
        }
    }
    return false;
}

/// @brief Determine whether a virtual register carries boolean semantics.
///
/// @details Scans backwards through @p block up to @p searchLimit instructions
///          looking for a defining @c SETcc.  If it encounters another
///          definition of the virtual register the search stops, treating the
///          value as non-boolean.  This allows call lowering to avoid emitting
///          redundant boolean materialisation instructions.
///
/// @param block Basic block containing the candidate value.
/// @param searchLimit Number of instructions to inspect starting from the end
///        of the block.
/// @param vreg Virtual register identifier being classified.
/// @return True when @p vreg is produced by a recognised boolean pattern.
[[nodiscard]] bool isI1Value(const MBasicBlock &block, std::size_t searchLimit, uint16_t vreg) {
    if (searchLimit > block.instructions.size()) {
        searchLimit = block.instructions.size();
    }
    while (searchLimit > 0) {
        --searchLimit;
        const auto &instr = block.instructions[searchLimit];
        if (isBoolProducer(instr, vreg)) {
            return true;
        }
        if (!instr.operands.empty()) {
            if (const auto *reg = std::get_if<OpReg>(&instr.operands.front()); reg) {
                if (!reg->isPhys && reg->idOrPhys == vreg) {
                    return false;
                }
            }
        }
    }
    return false;
}

/// @brief Create a stack-relative memory operand at the supplied offset.
///
/// @details Emits an operand that indexes from @c RSP using the canonical stack
///          frame base chosen by the ABI.  The helper centralises the base
///          register choice so updates to stack layout behaviour remain local.
///
/// @param offset Byte offset from the stack pointer where the slot begins.
/// @return Memory operand pointing at the requested stack slot.
[[nodiscard]] Operand makeStackSlot(int32_t offset) {
    return makeMemOperand(makePhysBase(PhysReg::RSP), offset);
}

} // namespace

/// @brief Lower a high-level call plan into concrete Machine IR instructions.
///
/// @details Inserts argument setup and call instructions into @p block using the
///          placement index requested by the caller.  Register arguments are
///          copied into their ABI-assigned registers, stack arguments are
///          written into aligned outgoing slots, and scratch registers are used
///          when operands need shuffling.  The helper also updates @p frame with
///          the amount of outgoing stack space consumed so frame construction
///          can reserve sufficient storage later in the pipeline.
///
/// @param block Machine basic block receiving the lowered call sequence.
/// @param insertIdx Instruction index at which new instructions should be
///        inserted.
/// @param plan Description of argument locations and scratch requirements.
/// @param target Target-specific information such as register assignments.
/// @param frame Frame summary updated with outgoing stack usage.
void lowerCall(MBasicBlock &block,
               std::size_t insertIdx,
               const CallLoweringPlan &plan,
               const TargetInfo &target,
               FrameInfo &frame) {
    assert(insertIdx <= block.instructions.size() && "insert index out of range");

    auto insertIt = block.instructions.begin();
    std::advance(insertIt, static_cast<std::ptrdiff_t>(insertIdx));

    auto insertInstr = [&](MInstr instr) {
        insertIt = block.instructions.insert(insertIt, std::move(instr));
        ++insertIt;
    };

    const bool isWin64 = target.shadowSpace != 0;
    const CallArgLayout layout = common::planCallArgs(
        plan.args,
        CallArgLayoutConfig{.maxGPRArgs = target.maxGPRArgs,
                            .maxFPRArgs = target.maxFPArgs,
                            .slotModel = isWin64 ? CallSlotModel::UnifiedRegisterPositions
                                                 : CallSlotModel::IndependentRegisterBanks,
                            .variadicTailOnStack = false,
                            .numNamedArgs = plan.numNamedArgs});
    auto maybeDuplicateWin64VarArgFpr = [&](const CallArgLocation &loc, PhysReg xmmReg) {
        if (!isWin64 || !plan.isVarArg || !loc.inRegister || loc.cls != CallArgClass::FPR)
            return;
        if (loc.regIndex >= target.intArgOrder.size())
            return;

        // Win64 variadic/unprototyped calls must mirror FP register arguments
        // into the corresponding positional integer register slot as raw bits.
        insertInstr(MInstr::make(
            MOpcode::MOVQxr,
            {makePhysOperand(RegClass::GPR, target.intArgOrder[loc.regIndex]),
             makePhysOperand(RegClass::XMM, xmmReg)}));
    };

    // Stack alignment is handled statically by FrameLowering which folds
    // outgoingArgArea into frameSize and rounds up to kStackAlignment (16).
    // No dynamic padding subtraction is needed here — the frame-resident
    // outgoing arg space is already correctly aligned.

    // Two-pass approach to avoid clobbering vreg values during argument setup:
    // Pass 1: Copy all vreg arguments to their destinations (reading vregs first)
    // Pass 2: Set up all immediate arguments (can safely overwrite registers now)
    // For vreg args going to registers, use scratch to avoid reading clobbered values.

    // Pass 1: Handle all vreg (non-immediate) arguments first
    for (const auto &loc : layout.locations) {
        const auto &arg = plan.args[loc.argIndex];
        if (arg.isImm)
            continue;
        const auto currentIdx =
            static_cast<std::size_t>(std::distance(block.instructions.begin(), insertIt));

        if (loc.cls == CallArgClass::GPR) {
            const Operand src = makeVRegOperand(RegClass::GPR, arg.vreg);
            const Operand scratch = makePhysOperand(RegClass::GPR, kScratchGPR);
            if (isI1Value(block, currentIdx, arg.vreg)) {
                insertInstr(MInstr::make(MOpcode::MOVZXrr32, {scratch, src}));
            } else {
                insertInstr(MInstr::make(MOpcode::MOVrr, {scratch, src}));
            }

            if (loc.inRegister) {
                const PhysReg destReg = target.intArgOrder[loc.regIndex];
                insertInstr(MInstr::make(
                    MOpcode::MOVrr, {makePhysOperand(RegClass::GPR, destReg), scratch}));
            } else {
                const auto slotOffset = static_cast<int32_t>(target.shadowSpace +
                                                             loc.stackSlotIndex * kSlotSizeBytes);
                insertInstr(MInstr::make(MOpcode::MOVrm, {makeStackSlot(slotOffset), scratch}));
            }
        } else {
            if (loc.inRegister) {
                const PhysReg destReg = target.f64ArgOrder[loc.regIndex];
                insertInstr(MInstr::make(
                    MOpcode::MOVSDrr,
                    {makePhysOperand(RegClass::XMM, destReg),
                     makeVRegOperand(RegClass::XMM, arg.vreg)}));
                maybeDuplicateWin64VarArgFpr(loc, destReg);
            } else {
                const auto slotOffset = static_cast<int32_t>(target.shadowSpace +
                                                             loc.stackSlotIndex * kSlotSizeBytes);
                const Operand scratchXmm = makePhysOperand(RegClass::XMM, kScratchXMM);
                insertInstr(MInstr::make(
                    MOpcode::MOVSDrr, {scratchXmm, makeVRegOperand(RegClass::XMM, arg.vreg)}));
                insertInstr(MInstr::make(MOpcode::MOVSDrm, {makeStackSlot(slotOffset), scratchXmm}));
            }
        }
    }

    // Pass 2: Handle all immediate arguments (now safe to overwrite registers)
    for (const auto &loc : layout.locations) {
        const auto &arg = plan.args[loc.argIndex];
        if (!arg.isImm)
            continue;

        if (loc.cls == CallArgClass::GPR) {
            if (loc.inRegister) {
                const PhysReg destReg = target.intArgOrder[loc.regIndex];
                insertInstr(MInstr::make(
                    MOpcode::MOVri,
                    {makePhysOperand(RegClass::GPR, destReg), makeImmOperand(arg.imm)}));
            } else {
                const auto slotOffset = static_cast<int32_t>(target.shadowSpace +
                                                             loc.stackSlotIndex * kSlotSizeBytes);
                const Operand scratch = makePhysOperand(RegClass::GPR, kScratchGPR);
                insertInstr(MInstr::make(MOpcode::MOVri, {scratch, makeImmOperand(arg.imm)}));
                insertInstr(MInstr::make(MOpcode::MOVrm, {makeStackSlot(slotOffset), scratch}));
            }
        } else {
            if (loc.inRegister) {
                const PhysReg destReg = target.f64ArgOrder[loc.regIndex];
                const Operand scratchGpr = makePhysOperand(RegClass::GPR, kScratchGPR);
                insertInstr(MInstr::make(MOpcode::MOVri, {scratchGpr, makeImmOperand(arg.imm)}));
                insertInstr(MInstr::make(
                    MOpcode::MOVQrx, {makePhysOperand(RegClass::XMM, destReg), scratchGpr}));
                maybeDuplicateWin64VarArgFpr(loc, destReg);
            } else {
                const auto slotOffset = static_cast<int32_t>(target.shadowSpace +
                                                             loc.stackSlotIndex * kSlotSizeBytes);
                const Operand scratchGpr = makePhysOperand(RegClass::GPR, kScratchGPR);
                const Operand scratchXmm = makePhysOperand(RegClass::XMM, kScratchXMM);
                insertInstr(MInstr::make(MOpcode::MOVri, {scratchGpr, makeImmOperand(arg.imm)}));
                insertInstr(MInstr::make(MOpcode::MOVQrx, {scratchXmm, scratchGpr}));
                insertInstr(MInstr::make(MOpcode::MOVSDrm, {makeStackSlot(slotOffset), scratchXmm}));
            }
        }
    }

    const std::size_t stackBytes = target.shadowSpace + layout.stackSlotsUsed * kSlotSizeBytes;
    frame.outgoingArgArea =
        std::max(frame.outgoingArgArea, static_cast<int>(roundUpSize(stackBytes, kSlotSizeBytes)));

    // SysV AMD64 varargs: %al must carry the number of XMM registers used.
    // Win64 instead mirrors FP register args into the corresponding integer
    // lanes earlier in lowering and does not use %al.
    if (plan.isVarArg && target.shadowSpace == 0) {
        const Operand rax = makePhysOperand(RegClass::GPR, PhysReg::RAX);
        insertInstr(
            MInstr::make(MOpcode::MOVri,
                         {rax, makeImmOperand(static_cast<int64_t>(layout.fprRegsUsed))}));
    }
}

} // namespace viper::codegen::x64
