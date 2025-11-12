//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

namespace viper::codegen::x64
{

namespace
{

constexpr PhysReg kScratchGPR = PhysReg::R11;
constexpr PhysReg kScratchXMM = PhysReg::XMM15;

#ifndef NDEBUG
int callAlignmentCheckCounter = 0;
#endif

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

/// @brief Create a stack-relative memory operand at the supplied offset.
///
/// @details Emits an operand that indexes from @c RSP using the canonical stack
///          frame base chosen by the ABI.  The helper centralises the base
///          register choice so updates to stack layout behaviour remain local.
///
/// @param offset Byte offset from the stack pointer where the slot begins.
/// @return Memory operand pointing at the requested stack slot.
[[nodiscard]] Operand makeStackSlot(int32_t offset)
{
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

    // Pre-scan to determine total bytes of stack-based arguments for this call.
    std::size_t preGprUsed = 0;
    std::size_t preXmmUsed = 0;
    std::size_t preStackBytes = 0;
    for (const auto &argScan : plan.args)
    {
        if (argScan.kind == CallArg::GPR)
        {
            if (preGprUsed < kMaxGPRArgs)
            {
                ++preGprUsed;
            }
            else
            {
                preStackBytes += kSlotSizeBytes;
            }
        }
        else // XMM
        {
            if (preXmmUsed < kMaxXMMArgs)
            {
                ++preXmmUsed;
            }
            else
            {
                preStackBytes += kSlotSizeBytes;
            }
        }
    }

    // When stack arguments are present (or when alignment demands), ensure the
    // stack is 16-byte aligned at the call boundary by inserting a dynamic
    // padding subtraction before writing stack slots and restoring it after the
    // call. The padding accounts for the return address pushed by CALL.
    const int32_t padBytes = static_cast<int32_t>((16 - ((preStackBytes + 8) % 16)) % 16);
    if (padBytes != 0)
    {
        insertInstr(MInstr::make(MOpcode::ADDri,
                                 {makePhysOperand(RegClass::GPR, PhysReg::RSP),
                                  makeImmOperand(-static_cast<int64_t>(padBytes))}));
    }

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
                if (gprUsed < kMaxGPRArgs)
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
                if (xmmUsed < kMaxXMMArgs)
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
        std::max(frame.outgoingArgArea, static_cast<int>(roundUpSize(stackBytes, kSlotSizeBytes)));

    // SysV AMD64 varargs: %al must carry the number of XMM registers used.
    if (plan.isVarArg)
    {
        const Operand rax = makePhysOperand(RegClass::GPR, PhysReg::RAX);
        insertInstr(
            MInstr::make(MOpcode::MOVri, {rax, makeImmOperand(static_cast<int64_t>(xmmUsed))}));
    }

#ifndef NDEBUG
    const std::string callOkLabel = ".Lcall_ok_" + std::to_string(callAlignmentCheckCounter++);
    const Operand rax = makePhysOperand(RegClass::GPR, PhysReg::RAX);
    const Operand rsp = makePhysOperand(RegClass::GPR, PhysReg::RSP);
    insertInstr(MInstr::make(MOpcode::MOVrr, {rax, rsp}));
    insertInstr(MInstr::make(MOpcode::ANDri, {rax, makeImmOperand(15)}));
    insertInstr(MInstr::make(MOpcode::TESTrr, {rax, rax}));
    insertInstr(MInstr::make(MOpcode::JCC, {makeImmOperand(0), makeLabelOperand(callOkLabel)}));
    insertInstr(MInstr::make(MOpcode::UD2));
    insertInstr(MInstr::make(MOpcode::LABEL, {makeLabelOperand(callOkLabel)}));
#endif

    // If we inserted dynamic padding earlier, restore %rsp immediately after
    // the CALL. Advance insertion point past the CALL placeholder and emit ADD.
    if (padBytes != 0)
    {
        // Find the CALL at or after the current insertion position.
        auto seekIt = insertIt;
        while (seekIt != block.instructions.end() && seekIt->opcode != MOpcode::CALL)
        {
            ++seekIt;
        }
        if (seekIt != block.instructions.end())
        {
            ++seekIt; // position after CALL
            seekIt = block.instructions.insert(
                seekIt,
                MInstr::make(MOpcode::ADDri,
                             {makePhysOperand(RegClass::GPR, PhysReg::RSP),
                              makeImmOperand(static_cast<int64_t>(padBytes))}));
            // Maintain insertIt validity if we inserted exactly at insertIt
            // (not strictly required for remaining code paths).
        }
    }

    (void)plan;
}

} // namespace viper::codegen::x64
