//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements trap and exception-handling opcode handlers for the virtual
// machine interpreter.  The helpers decode VmError payloads, manage resume
// tokens, and bridge legacy error codes into structured trap reporting.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Opcode handlers for trap production and exception resumption.
/// @details The helper functions in this translation unit operate on
///          @ref VM frames to expose IL instructions that inspect, modify, and
///          raise trap state.  They coordinate with the runtime bridge to keep
///          legacy error codes interoperable with the structured trap model.

#include "vm/OpHandlers_Control.hpp"

#include "vm/Marshal.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "vm/err_bridge.hpp"

#include <cassert>
#include <sstream>
#include <string>

namespace il::vm::detail::control
{

/// @brief Extract fields from a VmError record and store them into registers.
///
/// @details The handler accepts an optional operand referencing a resume token
///          or error value.  When omitted it falls back to the frame's active
///          error.  Depending on the opcode variant it copies the requested
///          field (kind, code, instruction pointer, or line) into the result
///          register.  The helper never modifies control flow and simply returns
///          to the dispatcher.
///
/// @param vm Virtual machine instance (unused).
/// @param fr Frame providing the active error state.
/// @param in Instruction indicating which field to retrieve.
/// @param blocks Map of block labels to block pointers (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer (unused).
/// @return Execution result signalling normal continuation.
VM::ExecResult handleErrGet(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot operandSlot{};
    if (!in.operands.empty())
        operandSlot = VMAccess::eval(vm, fr, in.operands[0]);

    const VmError *error = resolveErrorToken(fr, operandSlot);

    Slot out{};
    switch (in.op)
    {
        case il::core::Opcode::ErrGetKind:
            out.i64 = static_cast<int64_t>(static_cast<int32_t>(error->kind));
            break;
        case il::core::Opcode::ErrGetCode:
            out.i64 = static_cast<int64_t>(error->code);
            break;
        case il::core::Opcode::ErrGetIp:
            out.i64 = static_cast<int64_t>(error->ip);
            break;
        case il::core::Opcode::ErrGetLine:
            out.i64 = static_cast<int64_t>(static_cast<int32_t>(error->line));
            break;
        default:
            out.i64 = 0;
            break;
    }

    ops::storeResult(fr, in, out);
    return {};
}

/// @brief No-op handler that marks the beginning of an exception region.
///
/// @details The opcode exists to keep the instruction stream aligned with the
///          source program; the runtime only needs to know about push/pop and
///          resume operations.  Consequently the handler intentionally performs
///          no work and returns immediately.
VM::ExecResult handleEhEntry(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
                             size_t &ip)
{
    (void)vm;
    (void)fr;
    (void)in;
    (void)blocks;
    (void)bb;
    (void)ip;
    return {};
}

/// @brief Register an exception handler block on the frame's handler stack.
///
/// @details Validates that a destination label accompanies the opcode, resolves
///          it to a basic block, and pushes a handler record capturing the
///          target block together with the instruction pointer snapshot.  The
///          snapshot enables resume.next to continue from the trapping
///          instruction's successor.
VM::ExecResult handleEhPush(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)vm;
    (void)bb;
    (void)ip;
    assert(!in.labels.empty() && "eh.push requires handler label");
    auto it = blocks.find(in.labels[0]);
    assert(it != blocks.end() && "eh.push target must exist");
    Frame::HandlerRecord record{};
    record.handler = it->second;
    record.ipSnapshot = ip;
    fr.ehStack.push_back(record);
    return {};
}

/// @brief Remove the most recently registered exception handler.
///
/// @details Pops the frame's handler stack when non-empty.  The opcode is a
///          safety net in case control flow leaves a protected region without a
///          corresponding resume, ensuring stale handlers do not survive across
///          scopes.
VM::ExecResult handleEhPop(VM &vm,
                           Frame &fr,
                           const il::core::Instr &in,
                           const VM::BlockMap &blocks,
                           const il::core::BasicBlock *&bb,
                           size_t &ip)
{
    (void)vm;
    (void)in;
    (void)blocks;
    (void)bb;
    (void)ip;
    if (!fr.ehStack.empty())
        fr.ehStack.pop_back();
    return {};
}

/// @brief Resume execution at the trapping instruction itself.
/// @details Validates the supplied resume token, ensuring it matches the
///          current frame and that the recorded target block still exists.
///          Successful resumes clear the frame's resume state, redirect the
///          current block pointer to the captured block, and reset the
///          instruction pointer to the trapping site.  Failures emit a
///          diagnostic via @ref trapInvalidResume.  Resume tokens are
///          single-use; consuming one invalidates it to prevent stale
///          resumptions after handler unwinding.
VM::ExecResult handleResumeSame(VM &vm,
                                Frame &fr,
                                const il::core::Instr &in,
                                const VM::BlockMap &blocks,
                                const il::core::BasicBlock *&bb,
                                size_t &ip)
{
    (void)blocks;
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.same: missing resume token operand");
        return {};
    }

    Slot tokSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Frame::ResumeState *token = expectResumeToken(fr, tokSlot);
    if (!token)
    {
        trapInvalidResume(fr, in, bb, "resume.same: requires an active resume token");
        return {};
    }
    if (!token->block)
    {
        trapInvalidResume(fr, in, bb, "resume.same: resume target is no longer available");
        return {};
    }
    fr.resumeState.valid = false;
    bb = token->block;
    ip = token->faultIp;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

/// @brief Resume execution at the instruction immediately following the trap.
///
/// @details Mirrors @ref handleResumeSame but jumps to the saved "next" program
///          counter recorded when the resume token was created.  This is used
///          for trap handlers that want to skip the trapping instruction rather
///          than re-executing it.
VM::ExecResult handleResumeNext(VM &vm,
                                Frame &fr,
                                const il::core::Instr &in,
                                const VM::BlockMap &blocks,
                                const il::core::BasicBlock *&bb,
                                size_t &ip)
{
    (void)blocks;
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.next: missing resume token operand");
        return {};
    }

    Slot tokSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Frame::ResumeState *token = expectResumeToken(fr, tokSlot);
    if (!token)
    {
        trapInvalidResume(fr, in, bb, "resume.next: requires an active resume token");
        return {};
    }
    if (!token->block)
    {
        trapInvalidResume(fr, in, bb, "resume.next: resume target is no longer available");
        return {};
    }
    fr.resumeState.valid = false;
    bb = token->block;
    ip = token->nextIp;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

/// @brief Resume execution by branching to an explicitly provided label.
///
/// @details Validates both the resume token and the destination label, emitting
///          detailed diagnostics when either is invalid.  On success the frame's
///          resume state is cleared and control transfers through
///          @ref branchToTarget to reuse branch argument propagation logic.
VM::ExecResult handleResumeLabel(VM &vm,
                                 Frame &fr,
                                 const il::core::Instr &in,
                                 const VM::BlockMap &blocks,
                                 const il::core::BasicBlock *&bb,
                                 size_t &ip)
{
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.label: missing resume token operand");
        return {};
    }

    Slot tokSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Frame::ResumeState *token = expectResumeToken(fr, tokSlot);
    if (!token)
    {
        trapInvalidResume(fr, in, bb, "resume.label: requires an active resume token");
        return {};
    }

    if (in.labels.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.label: missing destination label");
        return {};
    }

    const auto &label = in.labels[0];
    if (blocks.find(label) == blocks.end())
    {
        std::ostringstream os;
        os << "resume.label: unknown destination label '" << label << "'";
        trapInvalidResume(fr, in, bb, os.str());
        return {};
    }
    fr.resumeState.valid = false;
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

/// @brief Return the trap kind associated with the active error or provided token.
///
/// @details Evaluates an optional operand referring to a VmError and otherwise
///          falls back to the current trap token or the frame's active error.
///          The resulting kind is stored as an integer in the destination
///          register, enabling IL code to branch on trap categories.
VM::ExecResult handleTrapKind(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    const VmError *error = nullptr;
    if (!in.operands.empty())
    {
        Slot errorSlot = VMAccess::eval(vm, fr, in.operands[0]);
        error = reinterpret_cast<const VmError *>(errorSlot.ptr);
    }

    if (!error)
        error = vm_current_trap_token();
    if (!error)
        error = &fr.activeError;

    Slot out{};
    const auto kindValue = error ? static_cast<int32_t>(error->kind) : static_cast<int32_t>(TrapKind::RuntimeError);
    out.i64 = static_cast<int64_t>(kindValue);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Convert a legacy BASIC error code into a VmError trap token.
/// @details Evaluates the numeric error code operand, optionally captures an
///          additional string message, and initialises a freshly acquired trap
///          token.  The token inherits the mapped trap kind and retains the
///          original error code for diagnostic purposes before being written to
///          the result register.  This bridges the legacy `err` semantics into
///          the structured trap path so diagnostics remain consistent.
VM::ExecResult handleTrapErr(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
                             size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    (void)fr;

    Slot codeSlot = VMAccess::eval(vm, fr, in.operands[0]);
    const int32_t code = static_cast<int32_t>(codeSlot.i64);

    std::string message;
    if (in.operands.size() > 1)
    {
        Slot textSlot = VMAccess::eval(vm, fr, in.operands[1]);
        if (textSlot.str != nullptr)
        {
            auto view = fromViperString(textSlot.str);
            message.assign(view.begin(), view.end());
        }
    }

    VmError *token = vm_acquire_trap_token();
    token->kind = map_err_to_trap(code);
    token->code = code;
    token->ip = 0;
    token->line = -1;
    vm_store_trap_token_message(message);

    Slot out{};
    out.ptr = token;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Raise a trap immediately using the opcode-specific semantics.
///
/// @details Handles `trap`, `trap.err`, and `trap.from.err` forms by delegating
///          to the runtime trap helpers.  The function always marks the
///          execution result as returned so the interpreter unwinds to the
///          caller after the trap is raised.
VM::ExecResult handleTrap(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    switch (in.op)
    {
        case il::core::Opcode::Trap:
            vm_raise(TrapKind::DomainError);
            break;
        case il::core::Opcode::TrapFromErr:
        {
            Slot codeSlot = VMAccess::eval(vm, fr, in.operands[0]);
            const auto trapKind = map_err_to_trap(static_cast<int32_t>(codeSlot.i64));
            vm_raise(trapKind, static_cast<int32_t>(codeSlot.i64));
            break;
        }
        default:
            vm_raise(TrapKind::RuntimeError);
            break;
    }
    VM::ExecResult result{};
    result.returned = true;
    return result;
}

} // namespace il::vm::detail::control

