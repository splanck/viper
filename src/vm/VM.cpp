// File: src/vm/VM.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements stack-based virtual machine for IL.
// Key invariants: Inline literal cache retains one runtime handle per embedded-NUL
//                 string literal.
// Ownership/Lifetime: VM references module owned externally.
// Links: docs/il-guide.md#reference

#include "vm/VM.hpp"
#include "vm/VMConfig.hpp"
#include "vm/OpHandlers.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Marshal.hpp"
#include <algorithm>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

using namespace il::core;

namespace il::vm
{
namespace
{
thread_local VM *tlsActiveVM = nullptr; ///< Active VM for trap formatting.

/// @brief Manage thread-local active VM pointer for trap reporting.
struct ActiveVMGuard
{
    VM *previous = nullptr;

    explicit ActiveVMGuard(VM *vm) : previous(tlsActiveVM)
    {
        tlsActiveVM = vm;
    }

    ~ActiveVMGuard()
    {
        tlsActiveVM = previous;
    }
};

std::string opcodeMnemonic(Opcode op)
{
    const size_t index = static_cast<size_t>(op);
    if (index < kNumOpcodes)
    {
        const auto &info = getOpcodeInfo(op);
        if (info.name && info.name[0] != '\0')
            return info.name;
    }
    return std::string("opcode#") + std::to_string(static_cast<int>(op));
}

#if defined(VIPER_VM_TRACE)
constexpr bool kTraceHookEnabled = VIPER_VM_TRACE != 0;
#else
constexpr bool kTraceHookEnabled = true;
#endif

} // namespace

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

/// Materialise an IL @p Value into a runtime @c Slot within a given frame.
///
/// The evaluator reads temporaries from the frame's register file and converts
/// constant operands into the appropriate slot representation. Global addresses
/// and constant strings are resolved through the VM's string pool, which bridges
/// to the runtime's string handling.
///
/// Workflow: dispatch on the value kind, pull temporaries from @p fr.regs,
/// translate constants into the matching slot field, resolve globals via
/// @c strMap, and return the populated slot (or default slot if absent).
///
/// @param fr Active call frame supplying registers and pending parameters.
/// @param v  IL value to evaluate.
/// @returns A @c Slot containing the realised value; when the value is unknown,
///          a default-initialised slot is returned.
Slot VM::eval(Frame &fr, const Value &v)
{
    Slot s{};
    switch (v.kind)
    {
        case Value::Kind::Temp:
        {
            if (v.id < fr.regs.size())
                return fr.regs[v.id];

            const std::string fnName = fr.func ? fr.func->name : std::string("<unknown>");
            const BasicBlock *block = currentContext.block;
            const std::string blockLabel = block ? block->label : std::string();
            const auto loc = currentContext.loc;

            std::ostringstream os;
            os << "temp %" << v.id << " out of range (regs=" << fr.regs.size() << ") in function " << fnName;
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
            return s;
        }
        case Value::Kind::ConstInt:
            s.i64 = toI64(v);
            return s;
        case Value::Kind::ConstFloat:
            s.f64 = toF64(v);
            return s;
        case Value::Kind::ConstStr:
        {
            if (v.str.find('\0') == std::string::npos)
            {
                s.str = rt_const_cstr(v.str.c_str());
                return s;
            }

            auto [it, inserted] = inlineLiteralCache.try_emplace(v.str);
            if (inserted)
                it->second = rt_string_from_bytes(v.str.data(), v.str.size());
            s.str = it->second;
            return s;
        }
        case Value::Kind::GlobalAddr:
        {
            auto it = strMap.find(v.str);
            if (it == strMap.end())
                RuntimeBridge::trap(TrapKind::DomainError, "unknown global", {}, fr.func->name, "");
            else
                s.str = it->second;
            return s;
        }
        case Value::Kind::NullPtr:
            s.ptr = nullptr;
            return s;
    }
    return s;
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

/// Advance the interpreter by a single instruction and report early exits.
///
/// Performs debug checks, tracing, opcode dispatch, and post-step bookkeeping
/// once before returning either a pause sentinel or the function's return
/// value. When no instructions remain the default zero slot is produced to
/// mirror falling off the end of the function body.
///
/// @param st Current execution state for the active frame.
/// @return Optional slot when execution should stop; @c std::nullopt otherwise.
std::optional<Slot> VM::stepOnce(ExecState &st)
{
    if (!st.bb || st.ip >= st.bb->instructions.size())
    {
        clearCurrentContext();
        Slot s{};
        s.i64 = 0;
        return s;
    }

    const Instr &in = st.bb->instructions[st.ip];
    setCurrentContext(st.fr, st.bb, st.ip, in);

    if (auto pause = shouldPause(st, &in, false))
        return pause;

    tracer.onStep(in, st.fr);
    ++instrCount;
    auto res = executeOpcode(st.fr, in, st.blocks, st.bb, st.ip);

    if (res.returned)
        return res.value;

    if (res.jumped)
        debug.resetLastHit();
    else
        ++st.ip;

    if (auto pause = shouldPause(st, nullptr, true))
        return pause;

    return std::nullopt;
}

/// Handle a trap dispatch request raised while executing an instruction.
///
/// Trap dispatch re-enters the interpreter via exceptions, so this helper keeps
/// the catch-site in @c runFunctionLoop concise. When the signal targets the
/// current execution state the current context is cleared and the interpreter
/// restarts; otherwise the caller must rethrow to propagate the trap outward.
///
/// @param signal Raised dispatch request.
/// @param st     Active execution state to compare against the signal target.
/// @return @c true when handled for @p st; @c false when the signal targets a
///         different frame.
bool VM::handleTrapDispatch(const TrapDispatchSignal &signal, ExecState &st)
{
    if (signal.target != &st)
        return false;
    clearCurrentContext();
    return true;
}

Opcode VM::fetchOpcode(ExecState &st)
{
    st.exitRequested = false;
    st.pendingResult.reset();

    if (!st.bb || st.ip >= st.bb->instructions.size())
    {
        clearCurrentContext();
        Slot zero{};
        zero.i64 = 0;
        st.pendingResult = zero;
        st.exitRequested = true;
        st.currentInstr = nullptr;
        return Opcode::Trap;
    }

    st.currentInstr = &st.bb->instructions[st.ip];
    setCurrentContext(st.fr, st.bb, st.ip, *st.currentInstr);

    if (auto pause = shouldPause(st, st.currentInstr, false))
    {
        st.pendingResult = *pause;
        st.exitRequested = true;
        return st.currentInstr->op;
    }

    return st.currentInstr->op;
}

void VM::handleInlineResult(ExecState &st, const ExecResult &exec)
{
    if (exec.returned)
    {
        st.pendingResult = exec.value;
        st.exitRequested = true;
        return;
    }

    if (exec.jumped)
        debug.resetLastHit();
    else
        ++st.ip;

    if (auto pause = shouldPause(st, nullptr, true))
    {
        st.pendingResult = *pause;
        st.exitRequested = true;
        return;
    }

    st.pendingResult.reset();
    st.exitRequested = false;
}

[[noreturn]] void VM::trapUnimplemented(Opcode op)
{
    const std::string funcName = currentContext.function ? currentContext.function->name : std::string("<unknown>");
    const std::string blockLabel = currentContext.block ? currentContext.block->label : std::string();
    RuntimeBridge::trap(TrapKind::InvalidOperation,
                        "unimplemented opcode: " + opcodeMnemonic(op),
                        currentContext.loc,
                        funcName,
                        blockLabel);
    std::terminate();
}

bool VM::runLoopSwitch(ExecState &st)
{
    st.currentInstr = nullptr;

#define TRACE_STEP(INSTR_PTR)                                                                                     \
    do                                                                                                            \
    {                                                                                                             \
        const auto *_traceInstr = (INSTR_PTR);                                                                    \
        if (_traceInstr)                                                                                          \
        {                                                                                                         \
            ++instrCount;                                                                                         \
            if constexpr (kTraceHookEnabled)                                                                      \
                tracer.onStep(*_traceInstr, st.fr);                                                               \
        }                                                                                                         \
    } while (false)

    while (true)
    {
        const Opcode op = fetchOpcode(st);
        if (st.exitRequested)
            return true;

        switch (op)
        {
#define OP_SWITCH(NAME, ...)                                                                                       \
    case Opcode::NAME:                                                                                              \
    {                                                                                                               \
        TRACE_STEP(st.currentInstr);                                                                                \
        inline_handle_##NAME(st);                                                                                   \
        break;                                                                                                      \
    }
#define IL_OPCODE(NAME, ...) OP_SWITCH(NAME, __VA_ARGS__)
#include "il/core/Opcode.def"
#undef IL_OPCODE
#undef OP_SWITCH
        default:
            trapUnimplemented(op);
        }

        if (st.exitRequested)
            return true;
    }
}

/// Main interpreter loop for executing a function.
///
/// Iterates over instructions in the current basic block, invokes tracing,
/// dispatches opcodes via @c executeOpcode, and checks debug controls before and
/// after each step. The loop exits when a return is executed or a debug pause is
/// requested.
///
/// Workflow: while instructions remain, consult debug controls pre-step, trace
/// and count the instruction, execute it via @c executeOpcode, react to the
/// resulting control-flow signal (return/jump/advance), and finally re-check
/// debug controls before continuing.
///
/// @param st Mutable execution state containing frame and control flow info.
/// @return Return value of the function or special slot from debug control.
Slot VM::runFunctionLoop(ExecState &st)
{
    for (;;)
    {
        clearCurrentContext();
        try
        {
            bool finished = false;
            switch (dispatchKind)
            {
                case DispatchKind::FnTable:
                    finished = runLoopFnTable(st);
                    break;
                case DispatchKind::Switch:
                    finished = runLoopSwitch(st);
                    break;
                case DispatchKind::Threaded:
                {
#if VIPER_THREADING_SUPPORTED
                    finished = runLoopThreaded(st);
#else
                    finished = runLoopSwitch(st);
#endif
                    break;
                }
            }

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
            if (!handleTrapDispatch(signal, st))
                throw;
        }
    }
}

bool VM::runLoopFnTable(ExecState &st)
{
    st.pendingResult.reset();
    st.exitRequested = false;
    st.currentInstr = nullptr;

    while (true)
    {
        auto result = stepOnce(st);
        if (result.has_value())
        {
            st.pendingResult = *result;
            st.exitRequested = true;
            return true;
        }
    }
}

#if VIPER_THREADING_SUPPORTED
bool VM::runLoopThreaded(ExecState &st)
{
    st.pendingResult.reset();
    st.exitRequested = false;
    st.currentInstr = nullptr;

    const Instr *currentInstr = nullptr;
    Opcode op = Opcode::Trap;
    bool exitRequested = false;

    auto storeResultAndExit = [&](const Slot &slot) {
        st.pendingResult = slot;
        exitRequested = true;
    };

    auto fetchNextOpcode = [&]() -> Opcode {
        while (true)
        {
            if (!st.bb || st.ip >= st.bb->instructions.size())
            {
                clearCurrentContext();
                Slot zero{};
                zero.i64 = 0;
                storeResultAndExit(zero);
                return Opcode::Trap;
            }

            currentInstr = &st.bb->instructions[st.ip];
            st.currentInstr = currentInstr;
            setCurrentContext(st.fr, st.bb, st.ip, *currentInstr);

            if (auto pause = shouldPause(st, currentInstr, false))
            {
                storeResultAndExit(*pause);
                return currentInstr->op;
            }

            exitRequested = false;
            return currentInstr->op;
        }
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

#define DISPATCH_TO(OPCODE_VALUE)                                                                          \
    do                                                                                                      \
    {                                                                                                       \
        size_t dispatchIndex = static_cast<size_t>(OPCODE_VALUE);                                           \
        if (dispatchIndex >= kOpLabelCount - 1)                                                             \
            dispatchIndex = kOpLabelCount - 1;                                                              \
        goto *kOpLabels[dispatchIndex];                                                                     \
    } while (false)

    for (;;)
    {
        clearCurrentContext();
        try
        {
            op = fetchNextOpcode();
            if (exitRequested)
                return true;
            DISPATCH_TO(op);

#define OP_CASE(name, ...)                                                                                   \
    LBL_##name:                                                                                               \
    {                                                                                                         \
        TRACE_STEP(currentInstr);                                                                             \
        auto execResult = executeOpcode(st.fr, *currentInstr, st.blocks, st.bb, st.ip);                       \
        if (execResult.returned)                                                                              \
        {                                                                                                     \
            st.pendingResult = execResult.value;                                                              \
            return true;                                                                                      \
        }                                                                                                     \
        if (execResult.jumped)                                                                               \
            debug.resetLastHit();                                                                             \
        else                                                                                                  \
            ++st.ip;                                                                                          \
        if (auto pause = shouldPause(st, nullptr, true))                                                      \
        {                                                                                                     \
            st.pendingResult = *pause;                                                                        \
            return true;                                                                                      \
        }                                                                                                     \
        op = fetchNextOpcode();                                                                               \
        if (exitRequested)                                                                                    \
            return true;                                                                                      \
        DISPATCH_TO(op);                                                                                      \
    }

#define IL_OPCODE(name, ...) OP_CASE(name, __VA_ARGS__)
#include "il/core/Opcode.def"
#undef IL_OPCODE

#undef OP_CASE

        LBL_UNIMPL:
        {
            trapUnimplemented(op);
        }
    }
    catch (const TrapDispatchSignal &signal)
    {
        if (!handleTrapDispatch(signal, st))
            throw;
    }
    }

#undef DISPATCH_TO

    return st.pendingResult.has_value();
}
#endif

#undef TRACE_STEP

#define VM_DISPATCH_IMPL(DISPATCH, HANDLER_EXPR) HANDLER_EXPR
#define VM_DISPATCH(NAME) VM_DISPATCH_IMPL(NAME, &detail::OpHandlers::handle##NAME)
#define VM_DISPATCH_ALT(DISPATCH, HANDLER_EXPR) VM_DISPATCH_IMPL(DISPATCH, HANDLER_EXPR)
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

std::optional<std::string> VM::lastTrapMessage() const
{
    if (lastTrap.message.empty())
        return std::nullopt;
    return lastTrap.message;
}

void VM::setCurrentContext(Frame &fr, const BasicBlock *bb, size_t ip, const Instr &in)
{
    currentContext.function = fr.func;
    currentContext.block = bb;
    currentContext.instructionIndex = ip;
    currentContext.hasInstruction = true;
    currentContext.loc = in.loc;
}

void VM::clearCurrentContext()
{
    currentContext.function = nullptr;
    currentContext.block = nullptr;
    currentContext.instructionIndex = 0;
    currentContext.hasInstruction = false;
    currentContext.loc = {};
}

FrameInfo VM::buildFrameInfo(const VmError &error) const
{
    FrameInfo frame{};
    if (currentContext.function)
        frame.function = currentContext.function->name;
    else if (!runtimeContext.function.empty())
        frame.function = runtimeContext.function;
    else if (!lastTrap.frame.function.empty())
        frame.function = lastTrap.frame.function;

    frame.ip = error.ip;
    if (frame.ip == 0 && currentContext.hasInstruction)
        frame.ip = static_cast<uint64_t>(currentContext.instructionIndex);
    else if (frame.ip == 0 && lastTrap.frame.ip != 0)
        frame.ip = lastTrap.frame.ip;

    frame.line = error.line;
    if (frame.line < 0 && currentContext.loc.isValid())
        frame.line = static_cast<int32_t>(currentContext.loc.line);
    else if (frame.line < 0 && runtimeContext.loc.isValid())
        frame.line = static_cast<int32_t>(runtimeContext.loc.line);
    else if (frame.line < 0 && lastTrap.frame.line >= 0)
        frame.line = lastTrap.frame.line;

    frame.handlerInstalled = std::any_of(execStack.begin(), execStack.end(), [](const ExecState *st) {
        return st && !st->fr.ehStack.empty();
    });
    return frame;
}

std::string VM::recordTrap(const VmError &error, const FrameInfo &frame)
{
    lastTrap.error = error;
    lastTrap.frame = frame;
    lastTrap.message = vm_format_error(error, frame);
    if (!runtimeContext.message.empty())
    {
        lastTrap.message += ": ";
        lastTrap.message += runtimeContext.message;
        runtimeContext.message.clear();
    }
    return lastTrap.message;
}

VM *VM::activeInstance()
{
    return tlsActiveVM;
}

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
            const int32_t lineValue = faultLoc.isValid() ? static_cast<int32_t>(faultLoc.line) : -1;
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

[[noreturn]] void VM::throwForTrap(ExecState *target)
{
    throw TrapDispatchSignal(target);
}

} // namespace il::vm
