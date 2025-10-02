// MIT License. See LICENSE in the project root for full license information.
// File: src/vm/control_flow.cpp
// Purpose: Implement VM handlers for branching, calls, and traps.
// Key invariants: Control-flow handlers maintain block parameters and frame state.
// Ownership/Lifetime: Handlers mutate the active frame without persisting external state.
// Links: docs/il-guide.md#reference

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Value.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "vm/err_bridge.hpp"
#include "rt_string.h"
#include <string>
#include <algorithm>
#include <cassert>
#include <vector>
#include <sstream>

using namespace il::core;

namespace il::vm::detail
{
namespace
{
Frame::ResumeState *expectResumeToken(Frame &fr, const Slot &slot)
{
    auto *token = reinterpret_cast<Frame::ResumeState *>(slot.ptr);
    if (!token || token != &fr.resumeState || !token->valid)
        return nullptr;
    return token;
}

void trapInvalidResume(Frame &fr,
                       const Instr &in,
                       const BasicBlock *bb,
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
} // namespace

/// @brief Transfer control to a branch target and seed its parameter slots.
/// @param vm Active VM used to evaluate branch argument values.
/// @param fr Current frame receiving parameter updates for the successor block.
/// @param in Branch or terminator instruction describing successor labels and arguments.
/// @param idx Index of the branch label/argument tuple that should be taken.
/// @param blocks Mapping from block labels to IR blocks; lookup must succeed (verified).
/// @param bb Output reference updated to the resolved successor block pointer.
/// @param ip Output instruction index reset to start executing at the new block.
/// @return Execution result flagged as a jump without producing a value.
/// @note Invariant: the branch target must exist in @p blocks; malformed IL would have
///       been rejected earlier by verification. When present, branch arguments are
///       evaluated and copied into the target block parameters before control moves.
VM::ExecResult OpHandlers::branchToTarget(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          size_t idx,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    const auto &label = in.labels[idx];
    auto it = blocks.find(label);
    assert(it != blocks.end() && "invalid branch target");
    const BasicBlock *target = it->second;

    if (idx < in.brArgs.size())
    {
        const auto &args = in.brArgs[idx];
        const size_t limit = std::min(args.size(), target->params.size());
        for (size_t i = 0; i < limit; ++i)
        {
            const auto id = target->params[i].id;
            assert(id < fr.params.size());
            fr.params[id] = vm.eval(fr, args[i]);
        }
    }

    bb = target;
    ip = 0;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

/// @brief Handle an unconditional `br` terminator by taking the sole successor.
/// @param vm Active VM instance (forwarded to @ref branchToTarget for argument eval).
/// @param fr Current execution frame that will carry the successor's parameters.
/// @param in Instruction describing the successor label and optional argument tuple.
/// @param blocks Mapping of reachable blocks for the parent function.
/// @param bb Output reference receiving the successor block pointer.
/// @param ip Output instruction index reset to the start of the new block.
/// @return Execution result indicating a taken jump.
/// @note Invariant: the first label entry in @p in is the unconditional successor and
///       must resolve within @p blocks; argument propagation mirrors
///       @ref OpHandlers::branchToTarget.
VM::ExecResult OpHandlers::handleBr(VM &vm,
                                    Frame &fr,
                                    const Instr &in,
                                    const VM::BlockMap &blocks,
                                    const BasicBlock *&bb,
                                    size_t &ip)
{
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

/// @brief Handle a conditional `cbr` terminator by selecting between two successors.
/// @param vm Active VM used to evaluate the boolean branch condition.
/// @param fr Current frame whose parameters are rewritten for the chosen successor.
/// @param in Instruction providing the condition operand and two successor labels.
/// @param blocks Mapping of blocks for the current function; both labels must resolve.
/// @param bb Output reference receiving the chosen successor block pointer.
/// @param ip Output instruction index reset to the beginning of the successor block.
/// @return Execution result indicating a taken jump to the selected block.
/// @note Invariant: @p in must supply exactly two branch labels whose entries exist in
///       @p blocks. The truthiness of the evaluated `i1` operand selects between index
///       0 (true) and 1 (false) before delegating to @ref branchToTarget.
VM::ExecResult OpHandlers::handleCBr(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    Slot cond = vm.eval(fr, in.operands[0]);
    const size_t targetIdx = (cond.i64 != 0) ? 0 : 1;
    return branchToTarget(vm, fr, in, targetIdx, blocks, bb, ip);
}

/// @brief Handle a `ret` terminator by yielding control to the caller.
/// @param vm Active VM used to evaluate an optional return operand.
/// @param fr Current frame whose result is propagated through @ref VM::ExecResult.
/// @param in Instruction describing the return operand (if present).
/// @param blocks Unused lookup table required by the handler signature.
/// @param bb Unused basic block reference for this terminator.
/// @param ip Unused instruction pointer reference for this terminator.
/// @return Execution result flagged with @c returned and carrying an optional value.
/// @note Invariant: when a return operand exists it is evaluated exactly once and the
///       resulting slot is stored in the execution result; control never touches
///       subsequent instructions within the block.
VM::ExecResult OpHandlers::handleRet(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    VM::ExecResult result{};
    if (!in.operands.empty())
        result.value = vm.eval(fr, in.operands[0]);
    result.returned = true;
    return result;
}

/// @brief Handle direct and indirect function calls from within the VM.
/// @param vm Active VM responsible for resolving internal callee definitions.
/// @param fr Current frame providing argument values and storing call results.
/// @param in Instruction describing the callee symbol and operand list.
/// @param blocks Unused block map parameter required by the generic handler signature.
/// @param bb Current basic block, used only for diagnostic context if bridging.
/// @param ip Unused instruction pointer reference for this opcode.
/// @return Execution result representing normal fallthrough.
/// @note Invariant: all operand slots are evaluated prior to dispatch. If the callee
///       exists in @p vm.fnMap, the VM executes it natively; otherwise
///       @ref RuntimeBridge routes the call to the runtime. Any produced value is
///       stored via @ref ops::storeResult, ensuring the destination slot is updated.
VM::ExecResult OpHandlers::handleCall(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    std::vector<Slot> args;
    args.reserve(in.operands.size());
    for (const auto &op : in.operands)
        args.push_back(vm.eval(fr, op));

    Slot out{};
    auto it = vm.fnMap.find(in.callee);
    if (it != vm.fnMap.end())
        out = vm.execFunction(*it->second, args);
    else
        out = RuntimeBridge::call(vm.runtimeContext, in.callee, args, in.loc, fr.func->name, bb->label);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Handle `err.get_*` opcodes by materialising fields from the active error.
/// @param vm Active VM instance used to evaluate the error operand.
/// @param fr Current frame carrying the handler's active error record.
/// @param in Instruction identifying which error field to fetch.
/// @param blocks Unused block map required by the dispatch signature.
/// @param bb Unused current block reference required by the dispatch signature.
/// @param ip Unused instruction index reference required by the dispatch signature.
/// @return Execution result indicating normal continuation.
VM::ExecResult OpHandlers::handleErrGet(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot operandSlot{};
    if (!in.operands.empty())
        operandSlot = vm.eval(fr, in.operands[0]);

    const VmError *error = resolveErrorToken(fr, operandSlot);

    Slot out{};
    switch (in.op)
    {
        case Opcode::ErrGetKind:
            out.i64 = static_cast<int64_t>(static_cast<int32_t>(error->kind));
            break;
        case Opcode::ErrGetCode:
            out.i64 = static_cast<int64_t>(error->code);
            break;
        case Opcode::ErrGetIp:
            out.i64 = static_cast<int64_t>(error->ip);
            break;
        case Opcode::ErrGetLine:
            out.i64 = static_cast<int64_t>(static_cast<int32_t>(error->line));
            break;
        default:
            out.i64 = 0;
            break;
    }

    ops::storeResult(fr, in, out);
    return {};
}

/// @brief No-op handler executed when entering an exception handler block.
/// @param vm Active VM (unused) included for signature uniformity.
/// @param fr Current frame (unused) retained for future bookkeeping needs.
/// @param in Instruction metadata (unused) describing the eh.entry site.
/// @param blocks Unused block map reference required by the handler interface.
/// @param bb Current block pointer (unused) provided for signature parity.
/// @param ip Instruction pointer (unused) supplied for signature parity.
/// @return Execution result indicating normal fallthrough.
VM::ExecResult OpHandlers::handleEhEntry(VM &vm,
                                         Frame &fr,
                                         const Instr &in,
                                         const VM::BlockMap &blocks,
                                         const BasicBlock *&bb,
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

/// @brief Push a handler entry onto the frame's exception handler stack.
/// @param vm Active VM (unused) included for signature uniformity.
/// @param fr Current frame receiving the handler registration.
/// @param in Instruction carrying the handler label to install.
/// @param blocks Mapping from labels to blocks used to resolve the handler target.
/// @param bb Current block pointer (unused) retained for signature compatibility.
/// @param ip Instruction pointer snapshot recorded for resume.same semantics.
/// @return Execution result indicating normal fallthrough.
VM::ExecResult OpHandlers::handleEhPush(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
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

VM::ExecResult OpHandlers::handleEhPop(VM &vm,
                                       Frame &fr,
                                       const Instr &in,
                                       const VM::BlockMap &blocks,
                                       const BasicBlock *&bb,
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

VM::ExecResult OpHandlers::handleResumeSame(VM &vm,
                                            Frame &fr,
                                            const Instr &in,
                                            const VM::BlockMap &blocks,
                                            const BasicBlock *&bb,
                                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.same: missing resume token operand");
        return {};
    }

    Slot tokSlot = vm.eval(fr, in.operands[0]);
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

VM::ExecResult OpHandlers::handleResumeNext(VM &vm,
                                            Frame &fr,
                                            const Instr &in,
                                            const VM::BlockMap &blocks,
                                            const BasicBlock *&bb,
                                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.next: missing resume token operand");
        return {};
    }

    Slot tokSlot = vm.eval(fr, in.operands[0]);
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

VM::ExecResult OpHandlers::handleResumeLabel(VM &vm,
                                            Frame &fr,
                                            const Instr &in,
                                            const VM::BlockMap &blocks,
                                            const BasicBlock *&bb,
                                            size_t &ip)
{
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.label: missing resume token operand");
        return {};
    }

    Slot tokSlot = vm.eval(fr, in.operands[0]);
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

VM::ExecResult OpHandlers::handleTrapKind(VM &vm,
                                         Frame &fr,
                                         const Instr &in,
                                         const VM::BlockMap &blocks,
                                         const BasicBlock *&bb,
                                         size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    const VmError *error = nullptr;
    if (!in.operands.empty())
    {
        Slot errorSlot = vm.eval(fr, in.operands[0]);
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

VM::ExecResult OpHandlers::handleTrapErr(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    (void)fr;

    Slot codeSlot = vm.eval(fr, in.operands[0]);
    const int32_t code = static_cast<int32_t>(codeSlot.i64);

    std::string message;
    if (in.operands.size() > 1)
    {
        Slot textSlot = vm.eval(fr, in.operands[1]);
        if (textSlot.str != nullptr)
            message = rt_string_cstr(textSlot.str);
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

VM::ExecResult OpHandlers::handleTrap(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    switch (in.op)
    {
        case Opcode::Trap:
            vm_raise(TrapKind::DomainError);
            break;
        case Opcode::TrapFromErr:
        {
            Slot codeSlot = vm.eval(fr, in.operands[0]);
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

} // namespace il::vm::detail

