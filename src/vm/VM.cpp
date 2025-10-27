//===----------------------------------------------------------------------===//
//
// This file is part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the stack-based virtual machine that executes IL modules.  The VM
// owns dispatch strategies, inline caches, and runtime bridges while borrowing
// module data owned by callers.  This translation unit centralises instruction
// selection, trap handling, and the execution loop so the surrounding
// components (debugger, tools, and runtime glue) interact with a single,
// well-documented surface.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Virtual machine execution engine implementation.
/// @details Defines the interpreter core, dispatch drivers, and helper
///          utilities that cooperate to execute IL functions.  The
///          implementation balances modularity (pluggable dispatch) with
///          performance by caching hot metadata and minimising per-instruction
///          branching.

#include "vm/VM.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "vm/OpHandlers.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VMContext.hpp"
#include <algorithm>
#include <iostream>
#include <string>

namespace il::vm
{
std::string opcodeMnemonic(il::core::Opcode op);

using il::core::BasicBlock;
using il::core::Function;
using il::core::Instr;
using il::core::Opcode;
using il::core::Value;

/// @brief Interface for pluggable interpreter dispatch strategies.
struct VM::DispatchDriver
{
    /// @brief Ensure derived classes clean up correctly.
    virtual ~DispatchDriver() = default;

    /// @brief Execute the dispatch loop until the VM requests an exit.
    /// @details Implementations fetch instructions from @p state, invoke the
    ///          appropriate opcode handler, and update interpreter bookkeeping.
    /// @param vm Owning virtual machine instance.
    /// @param context Shared VM context (debugging/traps/etc.).
    /// @param state Execution state being driven.
    /// @return True when the dispatch loop terminated normally; false when the
    ///         VM asked to pause.
    virtual bool run(VM &vm, VMContext &context, ExecState &state) = 0;
};

/// @brief Custom deleter for dispatch-driver unique pointers.
/// @details Allows @c std::unique_ptr to own forward-declared dispatch
///          implementations without exposing their concrete types to headers.
/// @param driver Heap-allocated driver to destroy.
void VM::DispatchDriverDeleter::operator()(DispatchDriver *driver) const
{
    delete driver;
}

namespace detail
{

/// @brief Dispatch driver that uses an opcode-to-function table.
class FnTableDispatchDriver final : public VM::DispatchDriver
{
  public:
    /// @brief Execute the interpreter using the function-table strategy.
    /// @details Each iteration fetches the next instruction, looks up its
    ///          handler in the generated table, and executes it before checking
    ///          for traps or exit conditions.
    /// @param vm Virtual machine instance driving execution.
    /// @param state Execution state being advanced; the VM context is unused.
    /// @return True when the VM exited cleanly, false when a pause was requested.
    bool run(VM &vm, VMContext &, VM::ExecState &state) override
    {
        while (true)
        {
            vm.beginDispatch(state);

            const il::core::Instr *instr = nullptr;
            if (!vm.selectInstruction(state, instr))
                return state.exitRequested;

            vm.traceInstruction(*instr, state.fr);
            auto exec = vm.executeOpcode(state.fr, *instr, state.blocks, state.bb, state.ip);
            if (vm.finalizeDispatch(state, exec))
                return true;
        }
    }
};

/// @brief Dispatch driver that expands handlers as a large switch statement.
class SwitchDispatchDriver final : public VM::DispatchDriver
{
  public:
    /// @brief Execute the interpreter using a switch-based dispatch loop.
    /// @details Fetches each instruction and jumps directly into the
    ///          corresponding @c inline_handle_* implementation via a switch.
    /// @param vm Virtual machine instance.
    /// @param state Execution state being advanced.
    /// @return True when the VM exited normally, false when paused.
    bool run(VM &vm, VMContext &, VM::ExecState &state) override
    {
        while (true)
        {
            vm.beginDispatch(state);

            const il::core::Instr *instr = nullptr;
            if (!vm.selectInstruction(state, instr))
                return state.exitRequested;

            vm.dispatchOpcodeSwitch(state, *instr);

            if (state.exitRequested)
                return true;
        }
    }
};

#if VIPER_THREADING_SUPPORTED
/// @brief Dispatch driver that uses computed goto threading.
class ThreadedDispatchDriver final : public VM::DispatchDriver
{
  public:
    /// @brief Execute the interpreter using computed gotos for dispatch.
    /// @details Maintains a label table built from the opcode list and jumps
    ///          directly to handlers while catching trap signals so the VM can
    ///          resume execution without unwinding.
    /// @param vm Virtual machine instance.
    /// @param context Context used to handle traps and debugging requests.
    /// @param state Execution state under control of the dispatcher.
    /// @return True when execution terminated normally; false otherwise.
    bool run(VM &vm, VMContext &context, VM::ExecState &state) override
    {
        const il::core::Instr *currentInstr = nullptr;

        auto fetchNext = [&]() -> il::core::Opcode
        {
            vm.beginDispatch(state);

            const il::core::Instr *instr = nullptr;
            const bool hasInstr = vm.selectInstruction(state, instr);
            currentInstr = instr;
            if (!hasInstr)
                return instr ? instr->op : il::core::Opcode::Trap;
            return instr->op;
        };

        static void *kOpLabels[] = {
#include "vm/ops/generated/ThreadedLabels.inc"
        };

        static constexpr size_t kOpLabelCount = sizeof(kOpLabels) / sizeof(kOpLabels[0]);

#define DISPATCH_TO(OPCODE_VALUE)                                                                  \
    do                                                                                             \
    {                                                                                              \
        size_t index = static_cast<size_t>(OPCODE_VALUE);                                          \
        if (index >= kOpLabelCount - 1)                                                            \
            index = kOpLabelCount - 1;                                                             \
        goto *kOpLabels[index];                                                                    \
    } while (false)

        for (;;)
        {
            vm.clearCurrentContext();
            try
            {
                il::core::Opcode opcode = fetchNext();
                if (state.exitRequested)
                    return true;
                DISPATCH_TO(opcode);

#include "vm/ops/generated/ThreadedCases.inc"

            LBL_UNIMPL:
            {
                vm.trapUnimplemented(opcode);
            }
            }
            catch (const VM::TrapDispatchSignal &signal)
            {
                if (!context.handleTrapDispatch(signal, state))
                    throw;
            }
        }

#undef DISPATCH_TO
        return false; // Unreachable but placates control-flow analysis.
    }
};
#endif // VIPER_THREADING_SUPPORTED

} // namespace detail

/// @brief Construct a trap dispatch signal targeting a specific execution state.
/// @param targetState Execution state that should resume after trap handling.
VM::TrapDispatchSignal::TrapDispatchSignal(ExecState *targetState) : target(targetState) {}

/// @brief Retrieve the diagnostic message associated with trap dispatch signals.
const char *VM::TrapDispatchSignal::what() const noexcept
{
    return "VM trap dispatch";
}

/// @brief Locate and execute the module's @c main function.
///
/// The entry point is looked up by name in the cached function map and then
/// executed via @c execFunction. Any tracing or debugging configured on the VM
/// applies to the entire run.
///
/// Workflow: look up @c main in @c fnMap, report a diagnostic when absent, and
/// otherwise call @c execFunction with an empty argument list before forwarding
/// its return value.
///
/// @returns Signed 64-bit exit code produced by the program's @c main function.
/// @retval 1 When the module lacks an entry point, after printing "missing main".
int64_t VM::run()
{
    auto it = fnMap.find("main");
    if (it == fnMap.end())
    {
        std::cerr << "missing main" << std::endl;
        return 1;
    }
    return execFunction(*it->second, {}).i64;
}

/// @brief Dispatch and execute a single IL instruction.
///
/// A handler is selected from a static table based on the opcode and invoked to
/// perform the operation. Handlers such as @c handleCall and @c handleTrap
/// communicate with the runtime bridge for foreign function calls or traps.
///
/// Workflow: index into the opcode handler table, validate the presence of a
/// handler, and invoke it to mutate @p fr, adjust control-flow bookkeeping, and
/// produce an execution result summarising the effects.
///
/// @param fr     Current frame.
/// @param in     Instruction to execute.
/// @param blocks Mapping of block labels used for branch resolution.
/// @param bb     [in,out] Updated to the current basic block after any branch.
/// @param ip     [in,out] Instruction index within @p bb.
/// @return Execution result capturing control-flow effects and return value.
VM::ExecResult VM::executeOpcode(Frame &fr,
                                 const Instr &in,
                                 const std::unordered_map<std::string, const BasicBlock *> &blocks,
                                 const BasicBlock *&bb,
                                 size_t &ip)
{
    const auto &table = getOpcodeHandlers();
    const size_t index = static_cast<size_t>(in.op);
    OpcodeHandler handler = index < table.size() ? table[index] : nullptr;
    if (!handler)
    {
        RuntimeBridge::trap(TrapKind::InvalidOperation,
                            "unimplemented opcode: " + opcodeMnemonic(in.op),
                            {},
                            fr.func->name,
                            "");
        ExecResult res{};
        res.jumped = true;
        return res;
    }
    return handler(*this, fr, in, blocks, bb, ip);
}

/// @brief Determine whether execution should pause before or after an instruction.
///
/// This forwards to @c processDebugControl so the centralised debug logic
/// remains in @c VMDebug.cpp while callers have a clear, intention-revealing
/// helper. A non-empty result signals a pause or termination condition that the
/// interpreter loop must honour immediately.
///
/// @param st      Current execution state.
/// @param in      Instruction involved in the decision or @c nullptr when none.
/// @param postExec True when invoked after executing @p in.
/// @return Optional slot containing the pause sentinel.
std::optional<Slot> VM::shouldPause(ExecState &st, const Instr *in, bool postExec)
{
    return processDebugControl(st, in, postExec);
}

/// @brief Reset per-iteration state before dispatching an instruction.
/// @param state Execution state to prepare.
void VM::beginDispatch(ExecState &state)
{
    state.exitRequested = false;
    state.pendingResult.reset();
    state.currentInstr = nullptr;
}

/// @brief Select the next instruction to execute for the active state.
/// @details Handles basic-block exhaustion, updates the current context used for
///          diagnostics, and honours debugger pause requests triggered prior to
///          execution.
/// @param state Execution state being advanced.
/// @param instr Output pointer receiving the selected instruction when present.
/// @return False when execution should stop (due to exit or pause), true otherwise.
bool VM::selectInstruction(ExecState &state, const Instr *&instr)
{
    if (!state.bb || state.ip >= state.bb->instructions.size())
    {
        clearCurrentContext();
        Slot zero{};
        zero.i64 = 0;
        state.pendingResult = zero;
        state.exitRequested = true;
        state.currentInstr = nullptr;
        return false;
    }

    state.currentInstr = &state.bb->instructions[state.ip];
    instr = state.currentInstr;
    setCurrentContext(state.fr, state.bb, state.ip, *state.currentInstr);

    if (auto pause = shouldPause(state, state.currentInstr, false))
    {
        state.pendingResult = *pause;
        state.exitRequested = true;
        return false;
    }

    return true;
}

/// @brief Update tracing counters and emit optional trace callbacks.
/// @param instr Instruction that was just executed.
/// @param frame Frame providing operand storage for tracing.
void VM::traceInstruction(const Instr &instr, Frame &frame)
{
    ++instrCount;
#if !defined(VIPER_VM_TRACE) || VIPER_VM_TRACE
    tracer.onStep(instr, frame);
#else
    (void)frame;
#endif
}

/// @brief Finalise dispatch after executing an instruction.
/// @details Processes return values, control-flow changes, and debugger pauses to
///          determine whether the dispatch loop should continue.
/// @param state Execution state being advanced.
/// @param exec Result returned by the opcode handler.
/// @return True when execution of the enclosing function has completed.
bool VM::finalizeDispatch(ExecState &state, const ExecResult &exec)
{
    if (exec.returned)
    {
        state.pendingResult = exec.value;
        state.exitRequested = true;
        return true;
    }

    if (exec.jumped)
        debug.resetLastHit();
    else
        ++state.ip;

    if (auto pause = shouldPause(state, nullptr, true))
    {
        state.pendingResult = *pause;
        state.exitRequested = true;
        return true;
    }

    state.pendingResult.reset();
    state.exitRequested = false;
    return false;
}

/// @brief Instantiate a dispatch driver for the requested strategy.
/// @param kind Dispatch strategy to instantiate.
/// @return Unique pointer owning the created driver.
std::unique_ptr<VM::DispatchDriver, VM::DispatchDriverDeleter> VM::makeDispatchDriver(
    DispatchKind kind)
{
    switch (kind)
    {
        case DispatchKind::FnTable:
            return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(
                new detail::FnTableDispatchDriver());
        case DispatchKind::Switch:
            return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(
                new detail::SwitchDispatchDriver());
        case DispatchKind::Threaded:
#if VIPER_THREADING_SUPPORTED
            return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(
                new detail::ThreadedDispatchDriver());
#else
            return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(
                new detail::SwitchDispatchDriver());
#endif
    }
    return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(
        new detail::SwitchDispatchDriver());
}

/// @brief Execute the interpreter loop until the current function returns.
/// @param st Execution state for the active function call.
/// @return Slot containing the function's return value (or zero when absent).
Slot VM::runFunctionLoop(ExecState &st)
{
    VMContext context(*this);
    for (;;)
    {
        clearCurrentContext();
        try
        {
            if (!dispatchDriver)
                dispatchDriver = makeDispatchDriver(dispatchKind);

            const bool finished = dispatchDriver ? dispatchDriver->run(*this, context, st) : false;

            if (finished)
            {
                if (st.pendingResult)
                    return *st.pendingResult;
                Slot zero{};
                zero.i64 = 0;
                return zero;
            }
        }
        catch (const TrapDispatchSignal &signal)
        {
            if (!context.handleTrapDispatch(signal, st))
                throw;
        }
    }
}

/// @brief Release resources owned by the VM, including cached strings.
VM::~VM()
{
    for (auto &entry : strMap)
        rt_str_release_maybe(entry.second);
    strMap.clear();

    for (auto &entry : inlineLiteralCache)
        rt_str_release_maybe(entry.second);
    inlineLiteralCache.clear();
}

#include "vm/ops/generated/InlineHandlersImpl.inc"
#include "vm/ops/generated/SwitchDispatchImpl.inc"

/// @brief Execute function @p fn with optional arguments.
///
/// Prepares an execution state, then runs the interpreter loop. The callee's
/// execution participates fully in tracing, debugging, and runtime bridge
/// interactions triggered through individual instructions.
///
/// Workflow: create an execution state via @c prepareExecution, pass it to
/// @c runFunctionLoop, and propagate the returned slot to the caller.
///
/// @param fn   Function to execute.
/// @param args Argument slots passed to the entry block parameters.
/// @return Slot containing the function's return value.
Slot VM::execFunction(const Function &fn, const std::vector<Slot> &args)
{
    ActiveVMGuard guard(this);
    lastTrap = {};
    trapToken = {};
    auto st = prepareExecution(fn, args);
    st.callSiteBlock = currentContext.block;
    st.callSiteIp = currentContext.hasInstruction ? currentContext.instructionIndex : 0;
    st.callSiteLoc = currentContext.loc;

    /// @brief RAII helper that pushes/pops the execution stack around calls.
    struct ExecStackGuard
    {
        VM &vm;
        VM::ExecState *state;

        /// @brief Push the execution state onto the VM stack.
        ExecStackGuard(VM &vmRef, VM::ExecState &stRef) : vm(vmRef), state(&stRef)
        {
            vm.execStack.push_back(state);
        }

        /// @brief Pop the execution state if it is still the active frame.
        ~ExecStackGuard()
        {
            if (!vm.execStack.empty() && vm.execStack.back() == state)
                vm.execStack.pop_back();
        }
    } guardStack(*this, st);

    return runFunctionLoop(st);
}

/// @brief Return the number of instructions executed by the VM instance.
/// @return Cumulative instruction count since construction or last reset.
uint64_t VM::getInstrCount() const
{
    return instrCount;
}

/// @brief Record the currently executing instruction for diagnostics and traps.
/// @param fr Active frame containing the instruction.
/// @param bb Basic block housing the instruction.
/// @param ip Index of the instruction within the block.
/// @param in Instruction being executed.
void VM::setCurrentContext(Frame &fr, const BasicBlock *bb, size_t ip, const Instr &in)
{
    currentContext.function = fr.func;
    currentContext.block = bb;
    currentContext.instructionIndex = ip;
    currentContext.hasInstruction = true;
    currentContext.loc = in.loc;
}

/// @brief Reset the recorded execution context.
void VM::clearCurrentContext()
{
    currentContext.function = nullptr;
    currentContext.block = nullptr;
    currentContext.instructionIndex = 0;
    currentContext.hasInstruction = false;
    currentContext.loc = {};
}

/// @brief Populate trap metadata and redirect control to an active handler.
/// @details Walks the execution stack to locate the nearest handler, wiring up
///          resume state and error slots before transferring control.
/// @param error Error structure describing the trap condition; updated in place.
/// @return True when a handler was found and control transferred.
bool VM::prepareTrap(VmError &error)
{
    const BasicBlock *faultBlock = currentContext.block;
    size_t faultIp = currentContext.hasInstruction ? currentContext.instructionIndex : 0;
    il::support::SourceLoc faultLoc = currentContext.loc;

    for (auto it = execStack.rbegin(); it != execStack.rend(); ++it)
    {
        ExecState *st = *it;
        Frame &fr = st->fr;
        if (!fr.ehStack.empty())
        {
            const auto &record = fr.ehStack.back();
            fr.activeError = error;
            const uint64_t ipValue = static_cast<uint64_t>(faultIp);
            const int32_t lineValue = faultLoc.hasLine() ? static_cast<int32_t>(faultLoc.line) : -1;
            fr.activeError.ip = ipValue;
            fr.activeError.line = lineValue;
            error.ip = ipValue;
            error.line = lineValue;

            fr.resumeState.block = faultBlock;
            fr.resumeState.faultIp = faultIp;
            fr.resumeState.nextIp =
                faultBlock ? std::min(faultIp + 1, faultBlock->instructions.size()) : faultIp;
            fr.resumeState.valid = true;

            Slot errSlot{};
            errSlot.ptr = &fr.activeError;
            Slot tokSlot{};
            tokSlot.ptr = &fr.resumeState;

            if (!record.handler->params.empty())
            {
                const auto &params = record.handler->params;
                fr.params[params[0].id] = errSlot;
                if (params.size() > 1)
                    fr.params[params[1].id] = tokSlot;
            }

            st->bb = record.handler;
            st->ip = 0;
            st->skipBreakOnce = false;

            vm_clear_trap_token();
            throwForTrap(st);
            return true; // Unreachable but silences control-path warnings.
        }

        faultBlock = st->callSiteBlock;
        faultIp = st->callSiteIp;
        faultLoc = st->callSiteLoc;
    }
    return false;
}

/// @brief Throw a trap-dispatch signal targeting the supplied execution state.
/// @param target Execution state that should resume after handling the trap.
[[noreturn]] void VM::throwForTrap(ExecState *target)
{
    throw TrapDispatchSignal(target);
}

} // namespace il::vm
