//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements trap, error, and exception-handling opcodes.  The helpers share
// resume-token validation and route diagnostics through the runtime bridge so
// resumptions and error materialisation follow a single well-documented path.
//
//===----------------------------------------------------------------------===//

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
/// @note Resume tokens are single-use capabilities; once consumed they are
///       invalidated to prevent stale resumptions after handler unwinding.
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

/// @brief Materialise a runtime trap token from the legacy err/ codes.
/// @note The helper bridges err-based semantics into the structured trap path
///       so diagnostics and runtime handlers share a consistent VmError format.
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

