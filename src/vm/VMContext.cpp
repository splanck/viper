//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the execution-context helpers shared by all VM dispatch
// strategies.  The utilities in this file provide a structured view of the
// currently executing frame, expose trap-handling hooks, and wrap commonly
// reused pieces of dispatch logic so the threaded, switch, and function-table
// dispatchers can share behaviour without duplicating bookkeeping code.
//
//===----------------------------------------------------------------------===//

#include "vm/VMContext.hpp"

#include "vm/Marshal.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "il/core/Function.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"

#include <exception>
#include <sstream>
#include <string>

namespace il::vm
{
namespace
{
thread_local VM *tlsActiveVM = nullptr; ///< Active VM for trap reporting.

/// @brief Convert an opcode enumerator into a human-readable mnemonic.
///
/// Falls back to an auto-generated placeholder when the opcode descriptor lacks
/// a canonical name, keeping diagnostics readable even for experimental
/// opcodes.
///
/// @param op Opcode being executed.
/// @return Mnemonic string suitable for diagnostics.
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

/// @brief RAII guard that updates the thread-local active VM pointer.
///
/// @param vm VM instance that should become visible to trap reporting.
ActiveVMGuard::ActiveVMGuard(VM *vm) : previous(tlsActiveVM)
{
    tlsActiveVM = vm;
}

/// @brief Restore the previously active VM when the guard goes out of scope.
ActiveVMGuard::~ActiveVMGuard()
{
    tlsActiveVM = previous;
}

/// @brief Wrap a VM instance to expose execution-context helpers.
VMContext::VMContext(VM &vm) noexcept : vmInstance(&vm) {}

/// @brief Materialise the value represented by an IL operand within a frame.
///
/// Handles temporaries, constants, globals, and null pointers, converting them
/// into @ref Slot structures ready for opcode execution.  Out-of-range accesses
/// raise a trap so undefined register usage is surfaced immediately.
///
/// @param fr    Active frame containing register state.
/// @param value IL operand to evaluate.
/// @return Slot populated with the operand's runtime representation.
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
            os << "temp %" << value.id << " out of range (regs=" << fr.regs.size() << ") in function " << fnName;
            if (!blockLabel.empty())
                os << ", block " << blockLabel;
            if (loc.isValid())
            {
                os << ", at line " << loc.line;
                if (loc.column > 0)
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
            if (value.str.find('\0') == std::string::npos)
            {
                slot.str = rt_const_cstr(value.str.c_str());
                return slot;
            }

            auto [it, inserted] = vmInstance->inlineLiteralCache.try_emplace(value.str);
            if (inserted)
                it->second = rt_string_from_bytes(value.str.data(), value.str.size());
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

/// @brief Execute a single dispatch iteration and return any produced result.
///
/// The helper drives the VM through beginDispatch → select → execute → finalize
/// and returns a pending result when execution halts.
///
/// @param state Execution state containing dispatch pointers and registers.
/// @return Populated Slot when execution yielded a result; std::nullopt when the
///         VM should continue running.
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

/// @brief React to an asynchronous trap dispatch request.
///
/// Clears the VM's current context when the signal targets the provided
/// execution state, enabling trap unwinding to resume from a clean slate.
///
/// @param signal Trap notification produced by the runtime bridge.
/// @param state  Execution state potentially awaiting the trap.
/// @return True when the trap applied to @p state; false otherwise.
bool VMContext::handleTrapDispatch(const VM::TrapDispatchSignal &signal, VM::ExecState &state) const
{
    if (signal.target != &state)
        return false;
    vmInstance->clearCurrentContext();
    return true;
}

/// @brief Peek at the opcode of the next instruction to be executed.
///
/// Useful for debugging or scripted stepping when the caller needs the opcode
/// without mutating execution state beyond the standard dispatch bookkeeping.
///
/// @param state Execution state describing the current program counter.
/// @return Opcode for the next instruction or Opcode::Trap when dispatch should
///         halt.
il::core::Opcode VMContext::fetchOpcode(VM::ExecState &state) const
{
    vmInstance->beginDispatch(state);

    const il::core::Instr *instr = nullptr;
    if (!vmInstance->selectInstruction(state, instr))
        return instr ? instr->op : il::core::Opcode::Trap;

    return instr->op;
}

/// @brief Finalise dispatch state after an inline execution attempt.
///
/// Delegates to @ref VM::finalizeDispatch so helper-based execution paths can
/// reuse the same completion logic as the main dispatch loop.
///
/// @param state Execution state to update.
/// @param exec  Result produced by a helper opcode implementation.
void VMContext::handleInlineResult(VM::ExecState &state, const VM::ExecResult &exec) const
{
    vmInstance->finalizeDispatch(state, exec);
}

/// @brief Raise an invalid-operation trap for unimplemented opcodes.
///
/// Formats a diagnostic using the current execution context and terminates the
/// process after reporting the trap.
///
/// @param opcode Opcode that lacks an implementation.
[[noreturn]] void VMContext::trapUnimplemented(il::core::Opcode opcode) const
{
    const std::string funcName = vmInstance->currentContext.function ? vmInstance->currentContext.function->name : std::string("<unknown>");
    const std::string blockLabel = vmInstance->currentContext.block ? vmInstance->currentContext.block->label : std::string();
    RuntimeBridge::trap(TrapKind::InvalidOperation,
                        "unimplemented opcode: " + opcodeMnemonic(opcode),
                        vmInstance->currentContext.loc,
                        funcName,
                        blockLabel);
    std::terminate();
}

/// @brief Forward an instruction to the trace sink for deterministic logging.
///
/// @param instr Instruction being executed.
/// @param frame Active frame supplying operand values.
void VMContext::traceStep(const il::core::Instr &instr, Frame &frame) const
{
    vmInstance->traceInstruction(instr, frame);
}

/// @brief Execute a single opcode using the VM's primary implementation.
///
/// This wrapper allows helpers to reuse the canonical execution routine while
/// still participating in guard logic such as trap handling.
///
/// @param frame Frame whose registers will be mutated.
/// @param instr Instruction to execute.
/// @param blocks Map of block labels to block pointers.
/// @param bb     Reference to the current block pointer.
/// @param ip     Instruction index within @p bb.
/// @return Result describing continuation, pause, or termination.
VM::ExecResult VMContext::executeOpcode(Frame &frame,
                                        const il::core::Instr &instr,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip) const
{
    return vmInstance->executeOpcode(frame, instr, blocks, bb, ip);
}

/// @brief Reset the VM's notion of the current execution context.
void VMContext::clearCurrentContext() const
{
    vmInstance->clearCurrentContext();
}

/// @brief Access the trace sink associated with the wrapped VM.
TraceSink &VMContext::traceSink() const noexcept
{
    return vmInstance->tracer;
}

/// @brief Access the debugger control surface for the wrapped VM.
DebugCtrl &VMContext::debugController() const noexcept
{
    return vmInstance->debug;
}

/// @brief Retrieve the underlying VM instance.
VM &VMContext::vm() const noexcept
{
    return *vmInstance;
}

/// @brief Fetch the VM currently active on this thread, if any.
VM *activeVMInstance()
{
    return tlsActiveVM;
}

/// @brief Dispatch helper forwarding @ref VM::eval through a temporary context.
Slot VM::eval(Frame &fr, const il::core::Value &value)
{
    VMContext ctx(*this);
    return ctx.eval(fr, value);
}

/// @brief Convenience wrapper invoking @ref VMContext::stepOnce on this VM.
std::optional<Slot> VM::stepOnce(ExecState &state)
{
    VMContext ctx(*this);
    return ctx.stepOnce(state);
}

/// @brief Propagate a trap dispatch signal through a VMContext helper.
bool VM::handleTrapDispatch(const TrapDispatchSignal &signal, ExecState &state)
{
    VMContext ctx(*this);
    return ctx.handleTrapDispatch(signal, state);
}

/// @brief Expose @ref VMContext::fetchOpcode for the active VM instance.
il::core::Opcode VM::fetchOpcode(ExecState &state)
{
    VMContext ctx(*this);
    return ctx.fetchOpcode(state);
}

/// @brief Forward inline execution results through @ref VMContext.
void VM::handleInlineResult(ExecState &state, const ExecResult &exec)
{
    VMContext ctx(*this);
    ctx.handleInlineResult(state, exec);
}

/// @brief Raise an unimplemented-opcode trap using the VMContext helper.
[[noreturn]] void VM::trapUnimplemented(il::core::Opcode opcode)
{
    VMContext ctx(*this);
    ctx.trapUnimplemented(opcode);
}

/// @brief Retrieve the VM currently marked active for trap reporting.
VM *VM::activeInstance()
{
    return activeVMInstance();
}

} // namespace il::vm
