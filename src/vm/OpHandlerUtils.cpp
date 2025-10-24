//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/OpHandlerUtils.cpp
// Purpose: Collect helper routines shared across opcode handler implementations.
// Key invariants: Register writes respect ownership rules and resume tokens must
//                 refer to the active frame state before use.
// Ownership/Lifetime: Functions borrow VM frames and never allocate persistent
//                     resources.
// Links: docs/runtime-vm.md#vm-dispatch
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
/// @brief Validate that a slot contains the active frame's resume token.
///
/// @details The VM encodes resume tokens as pointers to the owning frame's
///          @ref Frame::ResumeState record.  The helper converts the raw slot
///          payload back into a pointer, verifies that it refers to the current
///          frame, and checks the validity bit.  Returning @c nullptr signals to
///          callers that the operand either pointed at stale memory or referred
///          to a different frame, allowing handlers to trap with a precise
///          diagnostic.
///
/// @param fr Frame that owns the expected resume state.
/// @param slot Operand payload supplied by the opcode.
/// @return Pointer to the validated resume state or @c nullptr when invalid.
Frame::ResumeState *expectResumeToken(Frame &fr, const Slot &slot)
{
    auto *token = reinterpret_cast<Frame::ResumeState *>(slot.ptr);
    if (!token || token != &fr.resumeState || !token->valid)
        return nullptr;
    return token;
}

/// @brief Raise a runtime trap describing an invalid resume token access.
///
/// @details Constructs a context-rich error message capturing the function and
///          block currently executing before delegating to
///          @ref RuntimeBridge::trap.  Centralising the diagnostic keeps
///          handlers concise and ensures that all invalid resume situations are
///          reported with consistent wording.
///
/// @param fr Frame associated with the failing opcode.
/// @param in Instruction that attempted to consume the resume token.
/// @param bb Pointer to the block containing @p in.
/// @param detail Human-readable explanation of the failure mode.
void trapInvalidResume(Frame &fr,
                       const il::core::Instr &in,
                       const il::core::BasicBlock *bb,
                       std::string detail)
{
    const std::string functionName = fr.func ? fr.func->name : std::string{};
    const std::string blockLabel = bb ? bb->label : std::string{};
    RuntimeBridge::trap(TrapKind::InvalidOperation, detail, in.loc, functionName, blockLabel);
}

/// @brief Resolve an operand to an error token that can be inspected.
///
/// @details Opcodes may accept either an explicit error handle operand or rely
///          on the VM's implicit trap token.  The helper first checks whether the
///          operand contains a pointer to a @ref VmError.  When absent it queries
///          @ref vm_current_trap_token to reuse the globally active trap, falling
///          back to the frame's @ref Frame::activeError record as a final
///          default.  Centralising the lookup ensures that all handlers follow
///          the same precedence rules when examining error state.
///
/// @param fr Frame that owns the fallback error record.
/// @param slot Operand that may reference a @ref VmError.
/// @return Pointer to the resolved error description.
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
