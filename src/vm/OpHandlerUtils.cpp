//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Collects helper routines shared across the opcode handler implementations.
// The functions encapsulate the common bookkeeping required when writing back
// to frame registers so individual handlers remain focused on opcode semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements shared utilities for opcode handlers in the VM.
/// @details Most opcode handlers delegate register writes to these helpers so
///          string reference counting and register resizing logic live in one
///          well-documented location.

#include "vm/OpHandlerUtils.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"

namespace il::vm::detail
{
namespace ops
{
/// @brief Write an opcode result into the destination register while honouring
///        ownership semantics.
///
/// @details The helper resizes the register file on demand, retains/releases
///          runtime strings when the destination type is
///          @ref il::core::Type::Kind::Str, and then stores the slot payload.
///          Handlers delegate here to avoid duplicating register management
///          logic or forgetting to balance string reference counts.  When the
///          instruction lacks a result operand the function simply returns,
///          allowing opcode implementations to call it unconditionally.
///
/// @param fr Frame whose register file receives the result.
/// @param in Instruction describing the destination register and result type.
/// @param val Evaluated result slot to copy into the register file.
void storeResult(Frame &fr, const il::core::Instr &in, const Slot &val)
{
    if (!in.result)
        return;
    const size_t destIndex = *in.result;
    const bool hadRegister = destIndex < fr.regs.size();
    if (!hadRegister)
        fr.regs.resize(destIndex + 1);

    if (in.type.kind == il::core::Type::Kind::Str)
    {
        if (hadRegister)
            rt_str_release_maybe(fr.regs[destIndex].str);

        Slot stored = val;
        rt_str_retain_maybe(stored.str);
        fr.regs[destIndex] = stored;
        return;
    }

    fr.regs[destIndex] = val;
}
} // namespace ops

namespace control
{
Frame::ResumeState *expectResumeToken(Frame &fr, const Slot &slot)
{
    auto *token = reinterpret_cast<Frame::ResumeState *>(slot.ptr);
    if (!token || token != &fr.resumeState || !token->valid)
        return nullptr;
    return token;
}

void trapInvalidResume(Frame &fr,
                       const il::core::Instr &in,
                       const il::core::BasicBlock *bb,
                       std::string detail)
{
    const std::string functionName = fr.func ? fr.func->name : std::string{};
    const std::string blockLabel = bb ? bb->label : std::string{};
    RuntimeBridge::trap(TrapKind::InvalidOperation, detail, in.loc, functionName, blockLabel);
}

const VmError *resolveErrorToken(Frame &fr, const Slot &slot)
{
    const auto *error = reinterpret_cast<const VmError *>(slot.ptr);
    if (error)
        return error;

    if (const VmError *token = vm_current_trap_token())
        return token;

    return &fr.activeError;
}
} // namespace control
} // namespace il::vm::detail

