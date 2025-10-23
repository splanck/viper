//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/VM.cpp
// Purpose: Implement the IL virtual machine execution engine and dispatch loop.
// Key invariants: Inline literal caches retain one runtime handle per embedded-NUL
//                 string literal while execution contexts are reset between
//                 dispatch iterations.
// Ownership/Lifetime: VM borrows modules, runtime bridges, and frames from the
//                     caller; it only owns transient caches and dispatch drivers.
// Links: docs/il-guide.md#reference, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "vm/VM.hpp"
#include "vm/OpHandlers.hpp"
#include "vm/VMContext.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "vm/RuntimeBridge.hpp"
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

/// @brief Polymorphic dispatcher used to drive the interpreter loop.
///
/// @details Each driver implements a different dispatch strategy (function
///          table, switch, computed goto).  The interface returns @c true when
///          the interpreter should stop executing because the active frame has
///          produced a result or requested shutdown.
struct VM::DispatchDriver
{
    virtual ~DispatchDriver() = default;

    /// @brief Execute interpreter steps until a termination condition is met.
    /// @param vm Owning VM instance that exposes helper operations.
    /// @param context Runtime bridge context for trap dispatch.
    /// @param state Mutable execution state describing the active frame.
    /// @return True when execution finished or an exit was requested.
    virtual bool run(VM &vm, VMContext &context, ExecState &state) = 0;
};

/// @brief Delete helper for @ref DispatchDriver unique_ptrs.
/// @param driver Heap-allocated driver instance to destroy.
void VM::DispatchDriverDeleter::operator()(DispatchDriver *driver) const
{
    delete driver;
}

namespace detail
{

class FnTableDispatchDriver final : public VM::DispatchDriver
{
  public:
    /// @brief Dispatch by indexing into the opcode handler table.
    ///
    /// @details Each iteration prepares the dispatch state, selects the next
    ///          instruction, traces it for debugging, and invokes the handler
    ///          from the static opcode table returned by
    ///          @ref VM::getOpcodeHandlers.  The loop exits when
    ///          @ref VM::finalizeDispatch reports a return or exit request.
    ///
    /// @param vm Owning virtual machine.
    /// @param context Runtime bridge context (unused because traps bubble via
    ///        exceptions in this driver).
    /// @param state Execution state describing the active frame.
    /// @return True when execution finished or an exit was requested.
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

class SwitchDispatchDriver final : public VM::DispatchDriver
{
  public:
    /// @brief Dispatch using a large switch on the opcode enumerator.
    ///
    /// @details After selecting an instruction the driver switches on the
    ///          opcode and forwards to the corresponding inline handler via the
    ///          @c inline_handle_* helpers synthesised below.  Each handler may
    ///          request exit by setting @ref VM::ExecState::exitRequested.
    ///
    /// @param vm Active VM executing the program.
    /// @param context Runtime bridge context (unused because this strategy does
    ///        not wrap trap dispatch).
    /// @param state Execution state for the active frame.
    /// @return True when execution should stop.
    bool run(VM &vm, VMContext &, VM::ExecState &state) override
    {
        while (true)
        {
            vm.beginDispatch(state);

            const il::core::Instr *instr = nullptr;
            if (!vm.selectInstruction(state, instr))
                return state.exitRequested;

            switch (instr->op)
            {
#define OP_SWITCH(NAME, ...)                                                                                  \
    case il::core::Opcode::NAME:                                                                              \
    {                                                                                                         \
        vm.traceInstruction(*instr, state.fr);                                                                \
        vm.inline_handle_##NAME(state);                                                                       \
        break;                                                                                                \
    }
#define IL_OPCODE(NAME, ...) OP_SWITCH(NAME, __VA_ARGS__)
#include "il/core/Opcode.def"
#undef IL_OPCODE
#undef OP_SWITCH
                default:
                    vm.trapUnimplemented(instr->op);
            }

            if (state.exitRequested)
                return true;
        }
    }
};

#if VIPER_THREADING_SUPPORTED
class ThreadedDispatchDriver final : public VM::DispatchDriver
{
  public:
    /// @brief Dispatch via computed gotos for maximum interpreter throughput.
    ///
    /// @details Uses GCC-style labels-as-values to jump directly to the handler
    ///          for each opcode.  The loop repeatedly fetches the next
    ///          instruction, jumps to its label, executes it, and then resumes
    ///          dispatch.  Traps are caught and routed through the runtime
    ///          bridge context so asynchronous signals can resume execution.
    ///
    /// @param vm Owning VM providing helper operations.
    /// @param context VM context responsible for trap dispatch.
    /// @param state Execution state for the active frame.
    /// @return True when execution terminated or an exit was requested.
    bool run(VM &vm, VMContext &context, VM::ExecState &state) override
    {
        const il::core::Instr *currentInstr = nullptr;

        auto fetchNext = [&]() -> il::core::Opcode {
            vm.beginDispatch(state);

            const il::core::Instr *instr = nullptr;
            const bool hasInstr = vm.selectInstruction(state, instr);
            currentInstr = instr;
            if (!hasInstr)
                return instr ? instr->op : il::core::Opcode::Trap;
            return instr->op;
        };

#define OP_LABEL(name, ...) &&LBL_##name,
#define IL_OPCODE(name, ...) OP_LABEL(name, __VA_ARGS__)
        static void *kOpLabels[] = {
#include "il/core/Opcode.def"
            &&LBL_UNIMPL,
        };
#undef IL_OPCODE
#undef OP_LABEL

        static constexpr size_t kOpLabelCount = sizeof(kOpLabels) / sizeof(kOpLabels[0]);

#define DISPATCH_TO(OPCODE_VALUE)                                                                             \
    do                                                                                                      \
    {                                                                                                       \
        size_t index = static_cast<size_t>(OPCODE_VALUE);                                                   \
        if (index >= kOpLabelCount - 1)                                                                     \
            index = kOpLabelCount - 1;                                                                      \
        goto *kOpLabels[index];                                                                             \
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

#define OP_CASE(name, ...)                                                                                    \
    LBL_##name:                                                                                               \
    {                                                                                                         \
        vm.traceInstruction(*currentInstr, state.fr);                                                         \
        auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);               \
        if (vm.finalizeDispatch(state, exec))                                                                  \
            return true;                                                                                       \
        opcode = fetchNext();                                                                                  \
        if (state.exitRequested)                                                                               \
            return true;                                                                                       \
        DISPATCH_TO(opcode);                                                                                    \
    }

#define IL_OPCODE(name, ...) OP_CASE(name, __VA_ARGS__)
#include "il/core/Opcode.def"
#undef IL_OPCODE
#undef OP_CASE

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

/// Construct a trap dispatch signal targeting a specific execution state.
VM::TrapDispatchSignal::TrapDispatchSignal(ExecState *targetState) : target(targetState)
{
}

/// Retrieve the diagnostic message associated with trap dispatch signals.
const char *VM::TrapDispatchSignal::what() const noexcept
{
    return "trap dispatch";
}


/// Locate and execute the module's @c main function.
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

/// Dispatch and execute a single IL instruction.
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

/// Determine whether execution should pause before or after an instruction.
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

/// @brief Reset per-iteration dispatch bookkeeping before executing an opcode.
///
/// @details Clears any pending results, resets the exit flag, and forgets the
///          current instruction pointer so the subsequent selection step can
///          repopulate it.  Drivers call this before each fetch cycle.
///
/// @param state Execution state mutated by the dispatch loop.
void VM::beginDispatch(ExecState &state)
{
    state.exitRequested = false;
    state.pendingResult.reset();
    state.currentInstr = nullptr;
}

/// @brief Fetch the next instruction and establish the current execution context.
///
/// @details When the active basic block is exhausted the helper marks dispatch
///          as finished by setting @ref VM::ExecState::exitRequested and
///          synthesising a zero result.  Otherwise it updates the current
///          context used for debugging and trap reporting and honours pending
///          debugger pauses by returning @c false.
///
/// @param state Execution state describing the active frame.
/// @param instr [out] Populated with the next instruction when available.
/// @return True when an instruction was fetched; false when execution should stop.
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

/// @brief Notify tracing/debugging helpers that an instruction is about to execute.
///
/// @details Increments the global instruction counter and forwards the step to
///          the active tracer when tracing is enabled.  When tracing is disabled
///          the frame parameter is intentionally ignored.
///
/// @param instr Instruction about to execute.
/// @param frame Active frame supplying register state to the tracer.
void VM::traceInstruction(const Instr &instr, Frame &frame)
{
    ++instrCount;
#if !defined(VIPER_VM_TRACE) || VIPER_VM_TRACE
    tracer.onStep(instr, frame);
#else
    (void)frame;
#endif
}

/// @brief Apply the effects of a handler and decide whether to continue dispatching.
///
/// @details Captures return values, advances the instruction pointer for
///          fall-through execution, and honours debugger pause requests emitted
///          after the instruction completes.  When the handler performed a jump
///          the debug hit cache is reset so breakpoints trigger again on the new
///          location.
///
/// @param state Execution state mutated by the handler.
/// @param exec Summary of the handler's control-flow effects.
/// @return True when dispatch should stop for the current frame.
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

/// @brief Construct a dispatch driver implementing the requested strategy.
///
/// @details Allocates the concrete driver corresponding to @p kind.  When
///          threaded dispatch is unavailable the function transparently falls
///          back to the switch driver so callers do not need conditional logic.
///
/// @param kind Dispatch strategy requested by the caller.
/// @return Owning pointer to the constructed driver.
std::unique_ptr<VM::DispatchDriver, VM::DispatchDriverDeleter> VM::makeDispatchDriver(DispatchKind kind)
{
    switch (kind)
    {
        case DispatchKind::FnTable:
            return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(new detail::FnTableDispatchDriver());
        case DispatchKind::Switch:
            return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(new detail::SwitchDispatchDriver());
        case DispatchKind::Threaded:
#if VIPER_THREADING_SUPPORTED
            return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(new detail::ThreadedDispatchDriver());
#else
            return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(new detail::SwitchDispatchDriver());
#endif
    }
    return std::unique_ptr<DispatchDriver, DispatchDriverDeleter>(new detail::SwitchDispatchDriver());
}

/// @brief Run the interpreter loop until the active frame completes.
///
/// @details Lazily instantiates the configured dispatch driver, repeatedly
///          executes it, and handles trap dispatch requests routed through the
///          @ref VMContext.  When the frame finishes without producing an
///          explicit return value the helper synthesises an integer zero result
///          for compatibility with legacy callers.
///
/// @param st Execution state prepared by @ref prepareExecution.
/// @return Slot containing the function's returned value.
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

/// @brief Release runtime handles cached by the VM instance.
///
/// @details Drops all cached string handles (both in the map of global strings
///          and the inline literal cache) by calling the runtime release hook
///          before clearing the containers.  This ensures reference counts are
///          decremented even when the VM is destroyed without executing
///          additional code.
VM::~VM()
{
    for (auto &entry : strMap)
        rt_str_release_maybe(entry.second);
    strMap.clear();

    for (auto &entry : inlineLiteralCache)
        rt_str_release_maybe(entry.second);
    inlineLiteralCache.clear();
}
#define VM_DISPATCH_IMPL(DISPATCH, HANDLER_EXPR) HANDLER_EXPR
#define VM_DISPATCH(NAME) VM_DISPATCH_IMPL(NAME, &detail::handle##NAME)
#define VM_DISPATCH_ALT(DISPATCH, HANDLER_EXPR) VM_DISPATCH_IMPL(DISPATCH, HANDLER_EXPR)
/// @brief Inline opcode handler template invoked by switch dispatch.
///
/// @details Expands to a tiny wrapper that looks up the concrete handler for an
///          opcode, reports unimplemented opcodes as traps, executes the handler,
///          and forwards the result to @ref handleInlineResult so inline
///          dispatch mirrors the behaviour of the main interpreter loop.
///          Generated helpers rely on the same handler table used by
///          @ref FnTableDispatchDriver.
#define IL_OPCODE(NAME,                                                                            \
                  MNEMONIC,                                                                        \
                  RES_ARITY,                                                                       \
                  RES_TYPE,                                                                        \
                  MIN_OPS,                                                                         \
                  MAX_OPS,                                                                         \
                  OP0,                                                                             \
                  OP1,                                                                             \
                  OP2,                                                                             \
                  SIDE_EFFECTS,                                                                    \
                  SUCCESSORS,                                                                      \
                  TERMINATOR,                                                                      \
                  DISPATCH,                                                                        \
                  PARSE0,                                                                          \
                  PARSE1,                                                                          \
                  PARSE2,                                                                          \
                  PARSE3)                                                                          \
    void VM::inline_handle_##NAME(ExecState &st)                                                   \
    {                                                                                              \
        const Instr *instr = st.currentInstr;                                                      \
        if (!instr)                                                                                \
        {                                                                                          \
            trapUnimplemented(Opcode::NAME);                                                       \
        }                                                                                          \
        auto handler = DISPATCH;                                                                   \
        if (!handler)                                                                              \
        {                                                                                          \
            trapUnimplemented(Opcode::NAME);                                                       \
        }                                                                                          \
        ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);                  \
        handleInlineResult(st, exec);                                                              \
    }

#include "il/core/Opcode.def"
#undef IL_OPCODE
#undef VM_DISPATCH_ALT
#undef VM_DISPATCH
#undef VM_DISPATCH_IMPL

/// Execute function @p fn with optional arguments.
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
    struct ExecStackGuard
    {
        VM &vm;
        VM::ExecState *state;
        ExecStackGuard(VM &vmRef, VM::ExecState &stRef) : vm(vmRef), state(&stRef)
        {
            vm.execStack.push_back(state);
        }
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

/// @brief Record the currently executing frame and instruction for diagnostics.
///
/// @details Stores the frame, basic block, instruction index, and source
///          location so traps and debugger interactions can report precise
///          provenance.  Called immediately before executing an instruction.
///
/// @param fr Active frame.
/// @param bb Basic block containing the instruction.
/// @param ip Instruction index within @p bb.
/// @param in Instruction whose metadata provides the source location.
void VM::setCurrentContext(Frame &fr, const BasicBlock *bb, size_t ip, const Instr &in)
{
    currentContext.function = fr.func;
    currentContext.block = bb;
    currentContext.instructionIndex = ip;
    currentContext.hasInstruction = true;
    currentContext.loc = in.loc;
}

/// @brief Reset the recorded execution context.
///
/// @details Clears all context fields so trap reporting does not reference stale
///          instructions.  Called before potentially throwing and when the
///          dispatch loop resets state between instructions.
void VM::clearCurrentContext()
{
    currentContext.function = nullptr;
    currentContext.block = nullptr;
    currentContext.instructionIndex = 0;
    currentContext.hasInstruction = false;
    currentContext.loc = {};
}

/// @brief Populate error metadata and identify the catch handler for a trap.
///
/// @details Walks the execution stack from the innermost frame outward looking
///          for an active exception handler.  When one is found it captures the
///          fault location, seeds the handler parameters, rewrites the execution
///          state to resume at the handler, and throws a @ref TrapDispatchSignal
///          to unwind control back to the interpreter loop.  If no handler is
///          available the function returns @c false so the caller can escalate
///          the trap.
///
/// @param error Trap payload to populate with location metadata.
/// @return True when a handler was prepared and control will transfer there.
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
            fr.resumeState.nextIp = faultBlock ? std::min(faultIp + 1, faultBlock->instructions.size()) : faultIp;
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

            throwForTrap(st);
            return true; // Unreachable but silences control-path warnings.
        }

        faultBlock = st->callSiteBlock;
        faultIp = st->callSiteIp;
        faultLoc = st->callSiteLoc;
    }
    return false;
}

/// @brief Raise an exception used to unwind into trap handlers.
///
/// @details Throws a @ref TrapDispatchSignal targeting the specified execution
///          state.  The interpreter loop catches this exception and reroutes
///          control to the prepared handler frame.
///
/// @param target Execution state that should resume handling the trap.
[[noreturn]] void VM::throwForTrap(ExecState *target)
{
    throw TrapDispatchSignal(target);
}

} // namespace il::vm
