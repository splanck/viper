// File: src/vm/RuntimeBridge.cpp
// Purpose: Bridges VM to C runtime functions.
// License: MIT License
// Key invariants: None.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-guide.md#reference

#include "vm/RuntimeBridge.hpp"
#include "il/core/Opcode.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "vm/Marshal.hpp"
#include "vm/VM.hpp"

#include <array>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using il::support::SourceLoc;

namespace
{
using il::core::Opcode;
using il::runtime::RuntimeDescriptor;
using il::runtime::RtSig;
using il::vm::FrameInfo;
using il::vm::ResultBuffers;
using il::vm::RuntimeBridge;
using il::vm::RuntimeCallContext;
using il::vm::Slot;
using il::vm::TrapKind;
using il::vm::VM;
using il::vm::VmError;
using il::vm::vm_format_error;
using il::vm::vm_raise;

/// @brief Thread-local pointer to the runtime call context for active trap reporting.
thread_local RuntimeCallContext *tlsContext = nullptr;

using VmResult = Slot;
using Thunk = VmResult (*)(VM &, FrameInfo &, const RuntimeCallContext &);

static bool validateArgumentCount(const RuntimeDescriptor &desc,
                                  const std::string &name,
                                  const std::vector<Slot> &args,
                                  const SourceLoc &loc,
                                  const std::string &fn,
                                  const std::string &block)
{
    const auto expected = desc.signature.paramTypes.size();
    if (args.size() == expected)
        return true;

    std::ostringstream os;
    os << name << ": expected " << expected << " argument(s), got " << args.size();
    if (args.size() > expected)
        os << " (excess runtime operands)";
    RuntimeBridge::trap(TrapKind::DomainError, os.str(), loc, fn, block);
    return false;
}

static VmResult executeDescriptor(const RuntimeDescriptor &desc,
                                  Slot *argBegin,
                                  std::size_t argCount,
                                  const RuntimeCallContext &ctx)
{
    std::span<Slot> argSpan{};
    if (argBegin && argCount)
        argSpan = {argBegin, argCount};

    il::vm::PowStatus powStatus{};
    auto rawArgs = il::vm::marshalArguments(desc.signature, argSpan, powStatus);

    ResultBuffers buffers{};
    void *resultPtr = il::vm::resultBufferFor(desc.signature.retType.kind, buffers);
    desc.handler(rawArgs.empty() ? nullptr : rawArgs.data(), resultPtr);

    std::span<const Slot> readonlyArgs{};
    if (argBegin && argCount)
        readonlyArgs = {argBegin, argCount};
    auto trap = il::vm::classifyPowTrap(desc, powStatus, readonlyArgs, buffers);
    if (trap.triggered)
    {
        // RuntimeBridge::trap escalates into vm_raise when a VM is active.
        RuntimeBridge::trap(trap.kind, trap.message, ctx.loc, ctx.function, ctx.block);
        return Slot{};
    }

    return il::vm::assignCallResult(desc.signature, buffers);
}

static VmResult genericThunk(VM &vm, FrameInfo &frame, const RuntimeCallContext &ctx)
{
    (void)vm;
    (void)frame;
    return executeDescriptor(*ctx.descriptor, ctx.argBegin, ctx.argCount, ctx);
}

constexpr std::array<Thunk, static_cast<std::size_t>(RtSig::Count)> buildThunkTable()
{
    std::array<Thunk, static_cast<std::size_t>(RtSig::Count)> table{};
    table.fill(&genericThunk);
    return table;
}

const std::array<Thunk, static_cast<std::size_t>(RtSig::Count)> &thunkTable()
{
    static const auto table = buildThunkTable();
    return table;
}

struct ContextGuard
{
    RuntimeCallContext *previous;
    RuntimeCallContext *current;

    explicit ContextGuard(RuntimeCallContext &ctx) : previous(tlsContext), current(&ctx)
    {
        tlsContext = &ctx;
    }

    ~ContextGuard()
    {
        if (current)
        {
            current->loc = {};
            current->function.clear();
            current->block.clear();
            current->message.clear();
            current->descriptor = nullptr;
            current->argBegin = nullptr;
            current->argCount = 0;
        }
        tlsContext = previous;
    }
};

using Operands = std::span<const Slot>;

struct TrapCtx
{
    TrapKind kind;
    const std::string &message;
    const SourceLoc &loc;
    const std::string &function;
    const std::string &block;
    VM *vm = nullptr;
    VmError error{};
    FrameInfo frame{};
};

static void finalizeTrap(TrapCtx &ctx)
{
    if (ctx.vm)
    {
        vm_raise(ctx.kind);
        return;
    }

    std::string diagnostic = vm_format_error(ctx.error, ctx.frame);
    if (!ctx.message.empty())
    {
        diagnostic += ": ";
        diagnostic += ctx.message;
    }
    rt_abort(diagnostic.c_str());
}

static void handleOverflow(TrapCtx &ctx, Opcode opcode, const Operands &operands)
{
    (void)opcode;
    (void)operands;
    finalizeTrap(ctx);
}

static void handleDivByZero(TrapCtx &ctx, Opcode opcode, const Operands &operands)
{
    (void)opcode;
    (void)operands;
    finalizeTrap(ctx);
}

static void handleGenericTrap(TrapCtx &ctx)
{
    finalizeTrap(ctx);
}

} // namespace

/// @brief Entry point invoked from the C runtime when a trap occurs.
/// @details Serves as the external hook that the C runtime calls when
/// `rt_abort`-style routines detect a fatal condition. The VM stores call-site
/// context in a thread-local pointer via `RuntimeBridge::call`; this hook relays
/// the trap through `RuntimeBridge::trap` so diagnostics carry function, block,
/// and source information.
#if defined(__GNUC__)
extern "C" __attribute__((weak)) void vm_trap(const char *msg)
#else
extern "C" void vm_trap(const char *msg)
#endif
{
    const auto *ctx = il::vm::RuntimeBridge::activeContext();
    const char *trapMsg = msg ? msg : "trap";
    if (ctx)
        il::vm::RuntimeBridge::trap(TrapKind::DomainError, trapMsg, ctx->loc, ctx->function, ctx->block);
    else
        il::vm::RuntimeBridge::trap(TrapKind::DomainError, trapMsg, {}, "", "");
}

namespace il::vm
{

Slot RuntimeBridge::call(RuntimeCallContext &ctx,
                         const std::string &name,
                         const std::vector<Slot> &args,
                         const SourceLoc &loc,
                         const std::string &fn,
                         const std::string &block)
{
    ctx.loc = loc;
    ctx.function = fn;
    ctx.block = block;
    ContextGuard guard(ctx);
    Slot result{};

    const auto *desc = il::runtime::findRuntimeDescriptor(name);
    if (!desc)
    {
        std::ostringstream os;
        os << "attempted to call unknown runtime helper '" << name << '\'';
        RuntimeBridge::trap(TrapKind::DomainError, os.str(), loc, fn, block);
        return result;
    }
    if (!validateArgumentCount(*desc, name, args, loc, fn, block))
        return result;

    ctx.descriptor = desc;
    ctx.argBegin = args.empty() ? nullptr : const_cast<Slot *>(args.data());
    ctx.argCount = args.size();

    if (auto *vm = VM::activeInstance())
    {
        FrameInfo frame{};
        std::optional<RtSig> sigId = il::runtime::findRuntimeSignatureId(name);
        Thunk thunk = nullptr;
        if (sigId && static_cast<std::size_t>(*sigId) < thunkTable().size())
            thunk = thunkTable()[static_cast<std::size_t>(*sigId)];
        if (!thunk)
            thunk = &genericThunk;
        result = thunk(*vm, frame, ctx);
    }
    else
    {
        result = executeDescriptor(*desc, ctx.argBegin, ctx.argCount, ctx);
    }

    return result;
}

void RuntimeBridge::trap(TrapKind kind,
                         const std::string &msg,
                         const SourceLoc &loc,
                         const std::string &fn,
                         const std::string &block)
{
    TrapCtx ctx{kind, msg, loc, fn, block};
    ctx.vm = VM::activeInstance();
    if (ctx.vm)
    {
        if (loc.isValid())
        {
            ctx.vm->currentContext.loc = loc;
            ctx.vm->runtimeContext.loc = loc;
        }
        else
        {
            ctx.vm->runtimeContext.loc = {};
        }
        if (!fn.empty())
            ctx.vm->runtimeContext.function = fn;
        if (!block.empty())
            ctx.vm->runtimeContext.block = block;
        ctx.vm->runtimeContext.message = msg;
    }
    else
    {
        ctx.error.kind = kind;
        ctx.error.code = 0;
        ctx.error.ip = 0;
        ctx.error.line = loc.isValid() ? static_cast<int32_t>(loc.line) : -1;

        ctx.frame.function = fn.empty() ? std::string("<unknown>") : fn;
        ctx.frame.ip = 0;
        ctx.frame.line = ctx.error.line;
        ctx.frame.handlerInstalled = false;
    }

    constexpr Opcode trapOpcode = Opcode::Trap;
    const Operands noOperands{};

    switch (kind)
    {
    case TrapKind::Overflow:
        handleOverflow(ctx, trapOpcode, noOperands);
        return;
    case TrapKind::DivideByZero:
        handleDivByZero(ctx, trapOpcode, noOperands);
        return;
    default:
        handleGenericTrap(ctx);
        return;
    }
}

const RuntimeCallContext *RuntimeBridge::activeContext()
{
    return tlsContext;
}

} // namespace il::vm
