//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "vm/DispatchStrategy.hpp"
#include "vm/OpHandlers.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VMContext.hpp"

#include "rt_context.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace il::vm
{
/// @brief Retrieve the textual mnemonic associated with an opcode.
/// @details Defined in the opcode metadata translation unit; declared here so
///          diagnostic helpers can render human-readable opcode names when the
///          VM encounters unimplemented instructions.
/// @param op Opcode to describe.
/// @return Null-terminated mnemonic string.
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

// Forward declarations for strategy creation
std::unique_ptr<DispatchStrategy> createDispatchStrategy(VM::DispatchKind kind);

namespace detail
{

/// @brief Dispatch driver that uses an opcode-to-function table.
class FnTableDispatchDriver final : public VM::DispatchDriver
{
  private:
    std::unique_ptr<DispatchStrategy> strategy;

  public:
    FnTableDispatchDriver() : strategy(createDispatchStrategy(VM::DispatchKind::FnTable)) {}

    /// @brief Execute the interpreter using the function-table strategy.
    /// @details Delegates to the shared dispatch loop with the function table strategy.
    /// @param vm Virtual machine instance driving execution.
    /// @param ctx VM context for trap and debug handling.
    /// @param state Execution state being advanced.
    /// @return True when the VM exited cleanly, false when a pause was requested.
    bool run(VM &vm, VMContext &ctx, VM::ExecState &state) override
    {
        return runSharedDispatchLoop(vm, ctx, state, *strategy);
    }
};

/// @brief Dispatch driver that expands handlers as a large switch statement.
class SwitchDispatchDriver final : public VM::DispatchDriver
{
  private:
    std::unique_ptr<DispatchStrategy> strategy;

  public:
    SwitchDispatchDriver() : strategy(createDispatchStrategy(VM::DispatchKind::Switch)) {}

    /// @brief Execute the interpreter using a switch-based dispatch loop.
    /// @details Delegates to the shared dispatch loop with the switch strategy.
    /// @param vm Virtual machine instance.
    /// @param ctx VM context for trap and debug handling.
    /// @param state Execution state being advanced.
    /// @return True when the VM exited normally, false when paused.
    bool run(VM &vm, VMContext &ctx, VM::ExecState &state) override
    {
        return runSharedDispatchLoop(vm, ctx, state, *strategy);
    }
};

#if VIPER_THREADING_SUPPORTED
/// @brief Dispatch driver that uses computed goto threading.
/// @note This implementation still uses the original threaded dispatch
///       because computed gotos require embedding the loop directly.
///       A future refactor could extract the label handling into the strategy.
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
        // Note: The threaded dispatch requires special handling due to computed gotos.
        // For now, we keep the original implementation but could refactor this
        // to use a ThreadedStrategy that encapsulates the label table.
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

        // Compile-time verification: label table must have kNumOpcodes + 1 entries
        // (one per opcode plus LBL_UNIMPL fallback for bounds checking)
        static_assert(kOpLabelCount == il::core::kNumOpcodes + 1,
                      "Threaded label table size mismatch: expected kNumOpcodes + 1 labels");

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
                VIPER_VM_DISPATCH_BEFORE(context, opcode);
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
VM::ExecResult VM::executeOpcode(
    Frame &fr, const Instr &in, const BlockMap &blocks, const BasicBlock *&bb, size_t &ip)
{
    const size_t index = static_cast<size_t>(in.op);
    const auto &table = (handlerTable_ != nullptr) ? *handlerTable_ : getOpcodeHandlers();
    OpcodeHandler handler = index < table.size() ? table[index] : nullptr;
    if (!handler)
    {
        const std::string blockLabel = bb ? bb->label : std::string();
        std::string detail = "unimplemented opcode: " + opcodeMnemonic(in.op);
        if (!blockLabel.empty())
        {
            detail += " (block " + blockLabel + ')';
        }
        RuntimeBridge::trap(TrapKind::InvalidOperation, detail, in.loc, fr.func->name, blockLabel);
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
    state.hasPendingResult = false;
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
    if (!state.bb || state.ip >= state.bb->instructions.size()) [[unlikely]]
    {
        clearCurrentContext();
        Slot zero{};
        zero.i64 = 0;
        state.pendingResult = zero;
        state.hasPendingResult = true;
        state.exitRequested = true;
        state.currentInstr = nullptr;
        instr = nullptr;
        return false;
    }

    state.currentInstr = &state.bb->instructions[state.ip];
    instr = state.currentInstr;
    /// @brief Sets currentcontext value.
    setCurrentContext(state.fr, state.bb, state.ip, *state.currentInstr);

    if (auto pause = shouldPause(state, state.currentInstr, false)) [[unlikely]]
    {
        state.pendingResult = *pause;
        state.hasPendingResult = true;
        state.exitRequested = true;
        state.currentInstr = nullptr;
        instr = nullptr;
        return false;
    }

    return true;
}

/// @brief Update tracing counters and emit optional trace callbacks.
/// @param instr Instruction that was just executed.
/// @param frame Frame providing operand storage for tracing.
/// @details Uses the cached tracingActive_ flag for fast-path bypass when tracing
///          is disabled. This reduces per-instruction overhead from a function call
///          plus internal enabled() check to a single boolean test.
void VM::traceInstruction(const Instr &instr, Frame &frame)
{
    ++instrCount;
#if !defined(VIPER_VM_TRACE) || VIPER_VM_TRACE
    // Fast-path: skip tracer call entirely when tracing is disabled.
    // The tracingActive_ flag is cached from tracer.isEnabled() to avoid
    // repeated function calls on every instruction.
    if (tracingActive_) [[unlikely]]
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
    VIPER_VM_DISPATCH_AFTER(state,
                            state.currentInstr ? state.currentInstr->op : il::core::Opcode::Trap);
    if (state.exitRequested) [[unlikely]]
    {
        if (!state.hasPendingResult)
        {
            Slot s{};
            s.i64 = 1;
            state.pendingResult = s;
            state.hasPendingResult = true;
        }
        return true;
    }
    if (exec.returned) [[unlikely]]
    {
        state.pendingResult = exec.value;
        state.hasPendingResult = true;
        state.exitRequested = true;
        return true;
    }

    if (exec.jumped)
        debug.resetLastHit();
    else [[likely]]
        ++state.ip;

    if (auto pause = shouldPause(state, nullptr, true)) [[unlikely]]
    {
        state.pendingResult = *pause;
        state.hasPendingResult = true;
        state.exitRequested = true;
        return true;
    }

    // Note: exitRequested and hasPendingResult will be reset by beginDispatch
    // at the start of the next iteration, so we skip redundant writes here.
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
                if (st.hasPendingResult)
                    return st.pendingResult;
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

/// @brief Release resources owned by the VM, including cached strings, mutable globals, and runtime
/// context.
VM::~VM()
{
    for (auto &entry : strMap)
        rt_str_release_maybe(entry.second);
    strMap.clear();

    for (auto &entry : inlineLiteralCache)
        rt_str_release_maybe(entry.second);
    inlineLiteralCache.clear();

    for (auto &entry : mutableGlobalMap)
        std::free(entry.second);
    mutableGlobalMap.clear();

    // Cleanup and delete per-VM runtime context
    if (rtContext)
    {
        rt_context_cleanup(rtContext);
        delete rtContext;
        rtContext = nullptr;
    }
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

/// @brief Emit a tail-call event to the trace sink when enabled.
void VM::onTailCall(const Function *from, const Function *to)
{
#if !defined(VIPER_VM_TRACE) || VIPER_VM_TRACE
    tracer.onTailCall(from, to);
#else
    (void)from;
    (void)to;
#endif
}

#if VIPER_VM_OPCOUNTS
const std::array<uint64_t, il::core::kNumOpcodes> &VM::opcodeCounts() const
{
    return opCounts_;
}

void VM::resetOpcodeCounts()
{
    opCounts_.fill(0);
}

std::vector<std::pair<int, uint64_t>> VM::topOpcodes(std::size_t n) const
{
    std::vector<std::pair<int, uint64_t>> items;
    items.reserve(opCounts_.size());
    for (std::size_t i = 0; i < opCounts_.size(); ++i)
        if (opCounts_[i] != 0)
            items.emplace_back(static_cast<int>(i), opCounts_[i]);
    std::partial_sort(items.begin(),
                      items.begin() + std::min(n, items.size()),
                      items.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
    if (items.size() > n)
        items.resize(n);
    return items;
}
#endif

/// @brief Record the currently executing instruction for diagnostics and traps.
/// @param fr Active frame containing the instruction.
/// @param bb Basic block housing the instruction.
/// @param ip Index of the instruction within the block.
/// @param in Instruction being executed.
void VM::setCurrentContext(Frame &fr, const BasicBlock *bb, size_t ip, const Instr &in)
{
    // Optimize: only update fields that changed (common case: same function/block)
    if (currentContext.function != fr.func) [[unlikely]]
        currentContext.function = fr.func;
    if (currentContext.block != bb) [[unlikely]]
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
    // Derive trap context from execution stack for better performance.
    // This avoids updating currentContext on every instruction dispatch.
    const BasicBlock *faultBlock = nullptr;
    size_t faultIp = 0;
    il::support::SourceLoc faultLoc{};

    if (!execStack.empty())
    {
        // Get context from the active execution state
        const ExecState *activeState = execStack.back();
        faultBlock = activeState->bb;
        faultIp = activeState->ip;
        // Get source location from the current instruction
        if (faultBlock && faultIp < faultBlock->instructions.size())
            faultLoc = faultBlock->instructions[faultIp].loc;
    }
    else if (currentContext.hasInstruction)
    {
        // Fallback to currentContext if no active execution state
        faultBlock = currentContext.block;
        faultIp = currentContext.instructionIndex;
        faultLoc = currentContext.loc;
    }

    // Use index-based iteration to avoid potential iterator invalidation
    // if exception handling modifies the execution stack
    const size_t stackSize = execStack.size();
    for (size_t i = 0; i < stackSize; ++i)
    {
        // Iterate in reverse order (from top of stack)
        ExecState *st = execStack[stackSize - 1 - i];
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

            if (record.handler)
            {
                // Use reverse map for O(1) lookup instead of O(N*M) linear scan
                const Function *ownerFn = nullptr;
                auto it = blockToFunction.find(record.handler);
                if (it != blockToFunction.end())
                {
                    ownerFn = it->second;
                }

                if (ownerFn && fr.func != ownerFn)
                {
                    fr.func = ownerFn;
                    fr.regs.clear();

                    // Reuse/compute the maximum SSA value id from the cache to
                    // avoid rescanning the function on trap rebinding.
                    size_t maxSsaId = 0;
                    if (auto rc = regCountCache_.find(ownerFn); rc != regCountCache_.end())
                    {
                        maxSsaId = rc->second;
                    }
                    else
                    {
                        for (const auto &p : ownerFn->params)
                            maxSsaId = std::max(maxSsaId, static_cast<size_t>(p.id));
                        for (const auto &block : ownerFn->blocks)
                        {
                            for (const auto &p : block.params)
                                maxSsaId = std::max(maxSsaId, static_cast<size_t>(p.id));
                            for (const auto &instr : block.instructions)
                                if (instr.result)
                                    maxSsaId =
                                        std::max(maxSsaId, static_cast<size_t>(*instr.result));
                        }
                        regCountCache_.emplace(ownerFn, maxSsaId);
                    }

                    fr.regs.resize(maxSsaId + 1);
                    fr.params.assign(fr.regs.size(), std::nullopt);
                    st->blocks.clear();
                    for (const auto &b : ownerFn->blocks)
                        st->blocks[b.label] = &b;
                }
            }

            if (!record.handler->params.empty())
            {
                const auto &params = record.handler->params;
                if (params[0].id < fr.params.size())
                    fr.params[params[0].id] = errSlot;
                if (params.size() > 1)
                {
                    if (params[1].id < fr.params.size())
                        fr.params[params[1].id] = tokSlot;
                }
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
