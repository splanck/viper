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
/// @details Consults the opcode metadata table for a printable mnemonic and
///          falls back to a numbered placeholder when the entry lacks a name.
///          This guarantees that trap diagnostics always include a descriptive
///          token even for experimental opcodes.
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
/// @details Guards set the thread-local pointer on construction and record the
///          previously active VM so trap reporting can access the current
///          interpreter without explicit plumbing at each call site.
/// @param vm VM instance that will be considered active for the guard scope.
ActiveVMGuard::ActiveVMGuard(VM *vm) : previous(tlsActiveVM)
{
    tlsActiveVM = vm;
}

/// @brief Restore the previously active VM when the guard leaves scope.
/// @details Resets the thread-local pointer to the saved predecessor so nested
///          guards correctly restore whichever VM was running before the most
///          recent activation.
ActiveVMGuard::~ActiveVMGuard()
{
    tlsActiveVM = previous;
}

/// @brief Bind the context helper to a specific VM instance.
/// @details Stores the pointer to the owning VM so future helper calls can
///          delegate directly without incurring repeated lookups.
/// @param vm VM whose helpers should be exposed via this context.
VMContext::VMContext(VM &vm) noexcept : vmInstance(&vm)
{
#if VIPER_VM_OPCOUNTS
    config.enableOpcodeCounts = vm.enableOpcodeCounts;
#else
    config.enableOpcodeCounts = false;
#endif
}

/// @brief Evaluate an IL value within the current frame.
/// @details Resolves temporaries from the register file, marshals constants into
///          slot storage, and performs trap reporting for invalid references
///          (such as out-of-range temporaries or unknown globals).  String
///          constants are cached so embedded NUL literals survive round-tripping
///          into the runtime without losing data.
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
/// @details Selects the next instruction, forwards it to the tracer for
///          debugging visibility, executes it via the VM's opcode handlers, and
///          performs dispatch finalisation.  When the VM signals exit, the
///          pending result is surfaced; otherwise @c std::nullopt indicates that
///          execution should continue.
/// @param state Mutable execution state for the active frame.
/// @return Optional slot representing a completed execution result.
std::optional<Slot> VMContext::stepOnce(VM::ExecState &state) const
{
    vmInstance->beginDispatch(state);

    const il::core::Instr *instr = nullptr;
    if (!vmInstance->selectInstruction(state, instr))
        return state.pendingResult;

    // Dispatch hook before executing the opcode (counts, etc.).
#if VIPER_VM_OPCOUNTS
    VIPER_VM_DISPATCH_BEFORE((*this), instr->op);
#endif

    vmInstance->traceInstruction(*instr, state.fr);
    auto result = vmInstance->executeOpcode(state.fr, *instr, state.blocks, state.bb, state.ip);
    if (vmInstance->finalizeDispatch(state, result))
        return state.pendingResult;

    return std::nullopt;
}

/// @brief Handle a trap dispatch request emitted by the runtime bridge.
/// @details Compares the signal target with the supplied execution state and, on
///          a match, clears the VM's notion of the current context so that trap
///          handlers regain control without observing stale metadata.
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
/// @details Initiates dispatch to synchronise the instruction pointer before
///          returning the pending opcode.  When dispatch fails the trap opcode is
///          reported so debugging tools still receive a meaningful value.
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
/// @details Delegates to @ref VM::finalizeDispatch so inline handlers can reuse
///          the same clean-up logic as the main interpreter loop.
/// @param state Execution state receiving the result.
/// @param exec Result produced by an inline opcode handler.
void VMContext::handleInlineResult(VM::ExecState &state, const VM::ExecResult &exec) const
{
    vmInstance->finalizeDispatch(state, exec);
}

/// @brief Report an unimplemented opcode and terminate execution.
/// @details Builds a trap message containing the opcode mnemonic and current
///          execution context before routing it through the runtime bridge.
///          Termination follows immediately because continuing would leave the
///          VM in an undefined state.
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
    RuntimeBridge::trap(
        TrapKind::InvalidOperation, detail, vmInstance->currentContext.loc, funcName, blockLabel);
    std::terminate();
}

/// @brief Forward trace events to the underlying VM tracer.
/// @details Calls into @ref VM::traceInstruction so that instrumentation lives
///          in one place regardless of whether the interpreter is executing
///          inline or through the main dispatch loop.
/// @param instr Instruction being executed.
/// @param frame Active frame at the time of tracing.
void VMContext::traceStep(const il::core::Instr &instr, Frame &frame) const
{
    vmInstance->traceInstruction(instr, frame);
}

/// @brief Delegate opcode execution to the owning VM.
/// @details Thin wrapper that forwards to @ref VM::executeOpcode, keeping the
///          context helper responsible for routing rather than implementing the
///          opcode semantics itself.
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
/// @details Calls the owning VM's reset routine so subsequent traps do not refer
///          to stale frame metadata.
void VMContext::clearCurrentContext() const
{
    vmInstance->clearCurrentContext();
}

/// @brief Access the trace sink used by the VM.
/// @details Exposes the tracer so callers running through the helper can emit
///          trace events without reaching directly into the VM internals.
/// @return Reference to the trace sink owned by the VM.
TraceSink &VMContext::traceSink() const noexcept
{
    return vmInstance->tracer;
}

/// @brief Access the debug controller associated with the VM.
/// @details Provides mutable access so tooling can configure breakpoints while
///          still routing through the shared context helpers.
/// @return Reference to the debug controller.
DebugCtrl &VMContext::debugController() const noexcept
{
    return vmInstance->debug;
}

/// @brief Access the underlying VM instance.
/// @details Serves as a convenience for helpers that need to reach escape hatches
///          on the owning VM.
/// @return Reference to the VM bound to this context.
VM *VMContext::vm() const noexcept
{
    return vmInstance;
}

/// @brief Retrieve the currently active VM for the calling thread.
/// @details Returns the thread-local pointer established by
///          @ref ActiveVMGuard so trap bridges and other facilities can discover
///          the active interpreter.
/// @return Pointer to the VM previously installed via @ref ActiveVMGuard.
VM *activeVMInstance()
{
    return tlsActiveVM;
}

/// @brief Evaluate an IL value using a temporary context helper.
/// @details Constructs a short-lived @ref VMContext and forwards to
///          @ref VMContext::eval so the public VM API stays minimal while still
///          sharing the implementation with other helpers.
/// @param fr Frame providing registers and runtime state.
/// @param value IL value to evaluate.
/// @return Slot populated with the evaluated payload.
Slot VM::eval(Frame &fr, const il::core::Value &value)
{
    VMContext ctx(*this);
    return ctx.eval(fr, value);
}

/// @brief Execute a single interpreter step on behalf of the VM.
/// @details Delegates to @ref VMContext::stepOnce using a temporary context so
///          callers interact with a stable API while the shared helpers retain
///          the core control flow logic.
/// @param state Mutable execution state for the active frame.
/// @return Optional slot containing the program result when execution finished.
std::optional<Slot> VM::stepOnce(ExecState &state)
{
    ActiveVMGuard active(this);
    VMContext ctx(*this);
    struct ExecStackGuard
    {
        VM &vm;
        VM::ExecState *st;
        ExecStackGuard(VM &vmRef, VM::ExecState &stateRef) : vm(vmRef), st(&stateRef)
        {
            vm.execStack.push_back(st);
        }
        ~ExecStackGuard()
        {
            if (!vm.execStack.empty() && vm.execStack.back() == st)
                vm.execStack.pop_back();
        }
    } guard(*this, state);
    try
    {
        return ctx.stepOnce(state);
    }
    catch (const VM::TrapDispatchSignal &signal)
    {
        if (!ctx.handleTrapDispatch(signal, state))
            throw;
        // Trap dispatched; no completed result yet.
        return std::nullopt;
    }
}

/// @brief Forward a trap dispatch signal to the shared context helpers.
/// @details Routes the signal through @ref VMContext::handleTrapDispatch so
///          the invalidation logic remains centralised in the helper.
/// @param signal Trap dispatch payload generated by @ref RuntimeBridge.
/// @param state Execution state potentially referenced by the signal.
/// @return @c true when the signal targets @p state.
bool VM::handleTrapDispatch(const TrapDispatchSignal &signal, ExecState &state)
{
    VMContext ctx(*this);
    return ctx.handleTrapDispatch(signal, state);
}

/// @brief Inspect the opcode that would execute for the provided state.
/// @details Convenience wrapper that forwards to
///          @ref VMContext::fetchOpcode while hiding the context construction.
/// @param state Execution state being inspected.
/// @return Opcode slated for execution.
il::core::Opcode VM::fetchOpcode(ExecState &state)
{
    VMContext ctx(*this);
    return ctx.fetchOpcode(state);
}

/// @brief Propagate an inline execution result through the shared helpers.
/// @details Instantiates a context and forwards to
///          @ref VMContext::handleInlineResult so inline opcode handlers share
///          the same finalisation behaviour as the main interpreter.
/// @param state Execution state receiving the result.
/// @param exec Result produced by an inline opcode handler.
void VM::handleInlineResult(ExecState &state, const ExecResult &exec)
{
    VMContext ctx(*this);
    ctx.handleInlineResult(state, exec);
}

/// @brief Report an unimplemented opcode using the shared context helpers.
/// @details Creates a context and reuses
///          @ref VMContext::trapUnimplemented to emit diagnostics before
///          terminating.
/// @param opcode Opcode lacking an implementation.
[[noreturn]] void VM::trapUnimplemented(il::core::Opcode opcode)
{
    VMContext ctx(*this);
    ctx.trapUnimplemented(opcode);
}

/// @brief Retrieve the currently active VM for the calling thread.
/// @details Static convenience that simply forwards to the free-standing
///          @ref activeVMInstance helper.
/// @return Pointer to the active VM or @c nullptr when none is set.
VM *VM::activeInstance()
{
    return activeVMInstance();
}

} // namespace il::vm
