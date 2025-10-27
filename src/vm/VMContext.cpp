//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements helper utilities that expose the execution context of the virtual
// machine.  The routines centralise trap handling, operand evaluation, and debug
// forwarding so that individual dispatch strategies can share behaviour without
// duplicating state management.
//
//===----------------------------------------------------------------------===//

#include "vm/VMContext.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"

#include <exception>
#include <sstream>
#include <string>

namespace il::vm
{
namespace
{
thread_local VM *tlsActiveVM = nullptr; ///< Active VM for trap reporting.

/// @brief Resolve a human-readable mnemonic for an opcode.
///
/// Falls back to a numbered placeholder when the opcode metadata lacks a
/// printable name, ensuring trap messages always include a descriptive token.
///
/// @param op Opcode whose textual mnemonic is required.
/// @return Canonical mnemonic or a fallback string such as "opcode#17".
std::string opcodeMnemonic(il::core::Opcode op)
{
    const size_t index = static_cast<size_t>(op);
    if (index < il::core::kNumOpcodes)
    {
        const auto &info = getOpcodeInfo(op);
        if (info.name && info.name[0] != '\0')
            return info.name;
    }
    return std::string("opcode#") + std::to_string(static_cast<int>(op));
}

} // namespace

/// @brief Install a VM instance as the thread-local active VM.
///
/// Guards set the thread-local pointer on construction and restore the previous
/// value on destruction so trap reporting can access the currently executing VM
/// without explicit plumbing at each call site.
///
/// @param vm VM instance that will be considered active for the guard scope.
ActiveVMGuard::ActiveVMGuard(VM *vm) : previous(tlsActiveVM)
{
    tlsActiveVM = vm;
}

/// @brief Restore the previously active VM when the guard leaves scope.
ActiveVMGuard::~ActiveVMGuard()
{
    tlsActiveVM = previous;
}

/// @brief Bind the context helper to a specific VM instance.
///
/// @param vm VM whose helpers should be exposed via this context.
VMContext::VMContext(VM &vm) noexcept : vmInstance(&vm) {}

/// @brief Evaluate an IL value within the current frame.
///
/// Resolves temporaries from the register file, marshals constants into slot
/// storage, and performs trap reporting for invalid references (e.g. out-of-range
/// temporaries or unknown globals).  String constants are cached so embedded NUL
/// literals survive round-tripping into the runtime.
///
/// @param fr Frame providing registers and pending literals.
/// @param value IL value to evaluate.
/// @return Slot populated with the evaluated payload.
Slot VMContext::eval(Frame &fr, const il::core::Value &value) const
{
    Slot slot{};
    switch (value.kind)
    {
        case il::core::Value::Kind::Temp:
        {
            if (value.id < fr.regs.size())
                return fr.regs[value.id];

            const std::string fnName = fr.func ? fr.func->name : std::string("<unknown>");
            const il::core::BasicBlock *block = vmInstance->currentContext.block;
            const std::string blockLabel = block ? block->label : std::string();
            const auto loc = vmInstance->currentContext.loc;

            std::ostringstream os;
            os << "temp %" << value.id << " out of range (regs=" << fr.regs.size()
               << ") in function " << fnName;
            if (!blockLabel.empty())
                os << ", block " << blockLabel;
            if (loc.hasLine())
            {
                os << ", at line " << loc.line;
                if (loc.hasColumn())
                    os << ':' << loc.column;
            }
            else
            {
                os << ", at unknown location";
            }

            RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), loc, fnName, blockLabel);
            return slot;
        }
        case il::core::Value::Kind::ConstInt:
            slot.i64 = toI64(value);
            return slot;
        case il::core::Value::Kind::ConstFloat:
            slot.f64 = toF64(value);
            return slot;
        case il::core::Value::Kind::ConstStr:
        {
            auto [it, inserted] = vmInstance->inlineLiteralCache.try_emplace(value.str);
            if (inserted)
            {
                if (value.str.find('\0') == std::string::npos)
                    it->second = rt_const_cstr(value.str.c_str());
                else
                    it->second = rt_string_from_bytes(value.str.data(), value.str.size());
            }
            slot.str = it->second;
            return slot;
        }
        case il::core::Value::Kind::GlobalAddr:
        {
            auto it = vmInstance->strMap.find(value.str);
            if (it == vmInstance->strMap.end())
                RuntimeBridge::trap(TrapKind::DomainError, "unknown global", {}, fr.func->name, "");
            else
                slot.str = it->second;
            return slot;
        }
        case il::core::Value::Kind::NullPtr:
            slot.ptr = nullptr;
            return slot;
    }
    return slot;
}

/// @brief Execute a single interpreter step for the bound VM.
///
/// Selects the next instruction, traces it, executes it through the VM, and
/// finalises dispatch.  When the VM requests exit, the pending result is
/// returned; otherwise @c std::nullopt signals that execution should continue.
///
/// @param state Mutable execution state for the active frame.
/// @return Optional slot representing a completed execution result.
std::optional<Slot> VMContext::stepOnce(VM::ExecState &state) const
{
    vmInstance->beginDispatch(state);

    const il::core::Instr *instr = nullptr;
    if (!vmInstance->selectInstruction(state, instr))
        return state.pendingResult;

    vmInstance->traceInstruction(*instr, state.fr);
    auto result = vmInstance->executeOpcode(state.fr, *instr, state.blocks, state.bb, state.ip);
    if (vmInstance->finalizeDispatch(state, result))
        return state.pendingResult;

    return std::nullopt;
}

/// @brief Handle a trap dispatch request emitted by the runtime bridge.
///
/// When the signal targets the supplied execution state the VM clears its
/// current context, allowing the trap handler to resume control.
///
/// @param signal Trap dispatch payload generated by @ref RuntimeBridge.
/// @param state Execution state potentially referenced by the signal.
/// @return @c true if the signal referred to @p state.
bool VMContext::handleTrapDispatch(const VM::TrapDispatchSignal &signal, VM::ExecState &state) const
{
    if (signal.target != &state)
        return false;
    vmInstance->clearCurrentContext();
    return true;
}

/// @brief Inspect the opcode that would execute for the provided state.
///
/// The helper initiates dispatch so the instruction pointer is synchronised,
/// then returns the opcode for debugging tools.  When dispatch fails the trap
/// opcode is reported.
///
/// @param state Execution state being inspected.
/// @return Opcode slated for execution.
il::core::Opcode VMContext::fetchOpcode(VM::ExecState &state) const
{
    vmInstance->beginDispatch(state);

    const il::core::Instr *instr = nullptr;
    if (!vmInstance->selectInstruction(state, instr))
        return instr ? instr->op : il::core::Opcode::Trap;

    return instr->op;
}

/// @brief Propagate an inline execution result through the VM finalisation path.
///
/// @param state Execution state receiving the result.
/// @param exec Result produced by an inline opcode handler.
void VMContext::handleInlineResult(VM::ExecState &state, const VM::ExecResult &exec) const
{
    vmInstance->finalizeDispatch(state, exec);
}

/// @brief Report an unimplemented opcode and terminate execution.
///
/// Builds a trap message containing the opcode mnemonic and current context, then
/// terminates the process because continuing would leave the VM in an undefined
/// state.
///
/// @param opcode Opcode lacking an implementation.
[[noreturn]] void VMContext::trapUnimplemented(il::core::Opcode opcode) const
{
    const std::string funcName = vmInstance->currentContext.function
                                     ? vmInstance->currentContext.function->name
                                     : std::string("<unknown>");
    const std::string blockLabel =
        vmInstance->currentContext.block ? vmInstance->currentContext.block->label : std::string();
    std::string detail = "unimplemented opcode: " + opcodeMnemonic(opcode);
    if (!blockLabel.empty())
        detail += " (block " + blockLabel + ')';
    RuntimeBridge::trap(TrapKind::InvalidOperation,
                        detail,
                        vmInstance->currentContext.loc,
                        funcName,
                        blockLabel);
    std::terminate();
}

/// @brief Forward trace events to the underlying VM tracer.
///
/// @param instr Instruction being executed.
/// @param frame Active frame at the time of tracing.
void VMContext::traceStep(const il::core::Instr &instr, Frame &frame) const
{
    vmInstance->traceInstruction(instr, frame);
}

/// @brief Delegate opcode execution to the owning VM.
///
/// @param frame Active execution frame.
/// @param instr Instruction currently being executed.
/// @param blocks Cached block lookup for branch resolution.
/// @param bb Reference to the current basic block pointer.
/// @param ip Reference to the instruction index within @p bb.
/// @return Execution result capturing exit/trap status.
VM::ExecResult VMContext::executeOpcode(Frame &frame,
                                        const il::core::Instr &instr,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip) const
{
    return vmInstance->executeOpcode(frame, instr, blocks, bb, ip);
}

/// @brief Clear the VM's notion of the current execution context.
void VMContext::clearCurrentContext() const
{
    vmInstance->clearCurrentContext();
}

/// @brief Access the trace sink used by the VM.
///
/// @return Reference to the trace sink owned by the VM.
TraceSink &VMContext::traceSink() const noexcept
{
    return vmInstance->tracer;
}

/// @brief Access the debug controller associated with the VM.
///
/// @return Reference to the debug controller.
DebugCtrl &VMContext::debugController() const noexcept
{
    return vmInstance->debug;
}

/// @brief Access the underlying VM instance.
///
/// @return Reference to the VM bound to this context.
VM &VMContext::vm() const noexcept
{
    return *vmInstance;
}

/// @brief Retrieve the currently active VM for the calling thread.
///
/// @return Pointer to the VM previously installed via @ref ActiveVMGuard.
VM *activeVMInstance()
{
    return tlsActiveVM;
}

/// @brief Evaluate an IL value using a temporary context helper.
///
/// Thin wrapper that constructs a @ref VMContext to reuse the shared evaluation
/// logic, keeping the public VM API concise.
///
/// @param fr Frame providing registers and runtime state.
/// @param value IL value to evaluate.
/// @return Slot populated with the evaluated payload.
Slot VM::eval(Frame &fr, const il::core::Value &value)
{
    VMContext ctx(*this);
    return ctx.eval(fr, value);
}

/// @brief Execute a single interpreter step on behalf of the VM.
///
/// @param state Mutable execution state for the active frame.
/// @return Optional slot containing the program result when execution finished.
std::optional<Slot> VM::stepOnce(ExecState &state)
{
    VMContext ctx(*this);
    return ctx.stepOnce(state);
}

/// @brief Forward a trap dispatch signal to the shared context helpers.
///
/// @param signal Trap dispatch payload generated by @ref RuntimeBridge.
/// @param state Execution state potentially referenced by the signal.
/// @return @c true when the signal targets @p state.
bool VM::handleTrapDispatch(const TrapDispatchSignal &signal, ExecState &state)
{
    VMContext ctx(*this);
    return ctx.handleTrapDispatch(signal, state);
}

/// @brief Inspect the opcode that would execute for the provided state.
///
/// @param state Execution state being inspected.
/// @return Opcode slated for execution.
il::core::Opcode VM::fetchOpcode(ExecState &state)
{
    VMContext ctx(*this);
    return ctx.fetchOpcode(state);
}

/// @brief Propagate an inline execution result through the shared helpers.
///
/// @param state Execution state receiving the result.
/// @param exec Result produced by an inline opcode handler.
void VM::handleInlineResult(ExecState &state, const ExecResult &exec)
{
    VMContext ctx(*this);
    ctx.handleInlineResult(state, exec);
}

/// @brief Report an unimplemented opcode using the shared context helpers.
///
/// @param opcode Opcode lacking an implementation.
[[noreturn]] void VM::trapUnimplemented(il::core::Opcode opcode)
{
    VMContext ctx(*this);
    ctx.trapUnimplemented(opcode);
}

/// @brief Retrieve the currently active VM for the calling thread.
///
/// @return Pointer to the active VM or @c nullptr when none is set.
VM *VM::activeInstance()
{
    return activeVMInstance();
}

} // namespace il::vm
