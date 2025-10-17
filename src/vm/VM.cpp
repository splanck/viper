// File: src/vm/VM.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements stack-based virtual machine for IL.
// Key invariants: Inline literal cache retains one runtime handle per embedded-NUL
//                 string literal.
// Ownership/Lifetime: VM references module owned externally.
// Links: docs/il-guide.md#reference

#include "vm/VM.hpp"
#include "vm/OpHandlers.hpp"
#include "vm/VMContext.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "vm/RuntimeBridge.hpp"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace il::core;

namespace il::vm
{
namespace
{
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

bool isStringType(const il::core::Type &type)
{
    return type.kind == il::core::Type::Kind::Str;
}

void releaseFrameStrings(Frame &fr)
{
    if (!fr.func)
        return;

    const size_t regCount = fr.regs.size();
    std::vector<bool> stringSlots(regCount, false);

    auto markStringSlot = [&](unsigned id, const il::core::Type &type) {
        if (id < stringSlots.size() && isStringType(type))
            stringSlots[id] = true;
    };

    for (const auto &param : fr.func->params)
        markStringSlot(param.id, param.type);

    for (const auto &block : fr.func->blocks)
    {
        for (const auto &param : block.params)
            markStringSlot(param.id, param.type);

        for (const auto &instr : block.instructions)
            if (instr.result)
                markStringSlot(*instr.result, instr.type);
    }

    for (size_t idx = 0; idx < fr.regs.size(); ++idx)
        if (stringSlots[idx])
            rt_str_release_maybe(fr.regs[idx].str);

    for (size_t idx = 0; idx < fr.params.size(); ++idx)
    {
        if (idx < stringSlots.size() && stringSlots[idx] && fr.params[idx].has_value())
        {
            rt_str_release_maybe(fr.params[idx]->str);
            fr.params[idx].reset();
        }
    }
}

} // namespace

struct VM::DispatchDriver
{
    virtual ~DispatchDriver() = default;
    virtual bool run(VM &vm, VMContext &context, ExecState &state) = 0;
};

void VM::DispatchDriverDeleter::operator()(DispatchDriver *driver) const
{
    delete driver;
}

namespace detail
{

class FnTableDispatchDriver final : public VM::DispatchDriver
{
  public:
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

void VM::beginDispatch(ExecState &state)
{
    state.exitRequested = false;
    state.pendingResult.reset();
    state.currentInstr = nullptr;
}

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

void VM::traceInstruction(const Instr &instr, Frame &frame)
{
    ++instrCount;
#if !defined(VIPER_VM_TRACE) || VIPER_VM_TRACE
    tracer.onStep(instr, frame);
#else
    (void)frame;
#endif
}

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

Slot VM::runFunctionLoop(ExecState &st)
{
    struct FrameCleanup
    {
        Frame &frame;
        explicit FrameCleanup(Frame &fr) : frame(fr) {}
        ~FrameCleanup()
        {
            releaseFrameStrings(frame);
        }
    } cleanup(st.fr);

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
                {
                    Slot result = *st.pendingResult;
                    if (st.fr.func && st.fr.func->retType.kind == il::core::Type::Kind::Str && result.str)
                        rt_str_retain_maybe(result.str);
                    return result;
                }
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
