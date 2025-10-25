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

#include <algorithm>
#include <cassert>
#include <string>

namespace viper::codegen::x64
{

namespace
{

constexpr std::size_t kGprArgLimit = 6;
constexpr std::size_t kXmmArgLimit = 8;
constexpr std::size_t kSlotSizeBytes = 8;
constexpr PhysReg kScratchGPR = PhysReg::R11;
constexpr PhysReg kScratchXMM = PhysReg::XMM15;

#ifndef NDEBUG
int callAlignmentCheckCounter = 0;
#endif

/// @brief Create an operand referencing a concrete physical register.
///
/// @details Wraps @ref makePhysRegOperand to make call sites more readable when
///          they need an @ref Operand representing a hardware register.  The
///          helper preserves the register class supplied by the caller.
///
/// @param cls Register class describing the operand kind (GPR/XMM/etc.).
/// @param reg Physical register enumerator chosen by the caller.
/// @return Operand that refers to the requested physical register.
[[nodiscard]] Operand makePhysOperand(RegClass cls, PhysReg reg)
{
    return makePhysRegOperand(cls, static_cast<uint16_t>(reg));
}

/// @brief Build an @ref OpReg operand anchored to a physical GPR base register.
///
/// @details The call-lowering logic frequently needs an addressing base for
///          stack-relative memory operands.  This helper constructs the
///          canonical @c OpReg representation referencing @c reg in the general
///          purpose register class.
///
/// @param reg Physical register serving as the base of an address expression.
/// @return Register operand pointing at the supplied physical register.
[[nodiscard]] OpReg makePhysBase(PhysReg reg)
{
    return makePhysReg(RegClass::GPR, static_cast<uint16_t>(reg));
}

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

/// @brief Round @p bytes up to the nearest outbound argument slot size.
///
/// @details The SysV ABI requires outgoing arguments to be aligned on
///          eight-byte boundaries.  This helper normalises size computations so
///          stack reservations always honour that contract.
///
/// @param bytes Requested byte count.
/// @return Smallest multiple of eight that is greater than or equal to @p bytes.
[[nodiscard]] std::size_t alignToSlot(std::size_t bytes)
{
    if (bytes % kSlotSizeBytes == 0)
    {
        return bytes;
    }
    return bytes + (kSlotSizeBytes - (bytes % kSlotSizeBytes));
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

    insertInstr(MInstr::make(MOpcode::CALL, {makeLabelOperand(plan.calleeLabel)}));
}

} // namespace viper::codegen::x64
