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
#include <vector>

using il::support::SourceLoc;

namespace
{
using il::core::Opcode;
using il::runtime::RtSig;
using il::runtime::RuntimeDescriptor;
using il::vm::FrameInfo;
using il::vm::PowStatus;
using il::vm::ResultBuffers;
using il::vm::RuntimeBridge;
using il::vm::RuntimeCallContext;
using il::vm::Slot;
using il::vm::TrapKind;
using il::vm::VM;
using il::vm::VmError;
using il::vm::assignResult;
using il::vm::handlePowTrap;
using il::vm::marshalRuntimeArguments;
using il::vm::resultBufferFor;
using il::vm::vm_format_error;
using il::vm::vm_raise;

/// @brief Thread-local pointer to the runtime call context for active trap reporting.
thread_local RuntimeCallContext *tlsContext = nullptr;

using VmResult = Slot;
using Thunk = VmResult (*)(const RuntimeDescriptor &,
                           const std::vector<Slot> &,
                           const SourceLoc &,
                           const std::string &,
                           const std::string &,
                           RuntimeCallContext &);

VmResult callRuntimeHelper(const RuntimeDescriptor &desc,
                           const std::vector<Slot> &args,
                           const SourceLoc &loc,
                           const std::string &fn,
                           const std::string &block,
                           RuntimeCallContext &)
{
    PowStatus powStatus{};
    // Hidden parameters (e.g., pow status pointers) are appended alongside user arguments.
    auto rawArgs = marshalRuntimeArguments(desc.signature, args, powStatus);

    ResultBuffers buffers{};
    void *resultPtr = resultBufferFor(desc.signature.retType.kind, buffers);

    desc.handler(rawArgs.empty() ? nullptr : rawArgs.data(), resultPtr);

    if (handlePowTrap(desc.signature, desc.trapClass, powStatus, args, buffers, loc, fn, block))
        return {};

    Slot result{};
    assignResult(result, desc.signature.retType.kind, buffers);
    return result;
}

constexpr std::array<Thunk, static_cast<size_t>(RtSig::Count)> kThunks = [] {
    std::array<Thunk, static_cast<size_t>(RtSig::Count)> table{};
    table.fill(&callRuntimeHelper);
    return table;
}();

VmResult dispatchThunk(const RuntimeDescriptor &desc,
                       const std::vector<Slot> &args,
                       const SourceLoc &loc,
                       const std::string &fn,
                       const std::string &block,
                       RuntimeCallContext &ctx,
                       std::optional<RtSig> sigId)
{
    if (sigId && static_cast<size_t>(*sigId) < kThunks.size())
        return kThunks[static_cast<size_t>(*sigId)](desc, args, loc, fn, block, ctx);
    return callRuntimeHelper(desc, args, loc, fn, block, ctx);
}

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
        }
        tlsContext = previous;
    }
};

using OpCode = Opcode;
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
        // vm_raise relays the trap to the active interpreter loop without losing context.
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

static void handleOverflow(TrapCtx &ctx, OpCode opcode, const Operands &operands)
{
    (void)opcode;
    (void)operands;
    finalizeTrap(ctx);
}

static void handleDivByZero(TrapCtx &ctx, OpCode opcode, const Operands &operands)
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

/// @brief Dispatch a VM runtime call to the corresponding C implementation.
/// @details Establishes the trap bookkeeping for the duration of the call,
/// validates the arity against the lazily initialized dispatch table, and then
/// executes the bound C adapter. Any trap that fires while the callee runs is
/// able to surface precise context through the thread-local state populated
/// here.
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
    Slot res{};
    const auto *desc = il::runtime::findRuntimeDescriptor(name);
    if (!desc)
    {
        std::ostringstream os;
        os << "attempted to call unknown runtime helper '" << name << '\'';
        RuntimeBridge::trap(TrapKind::DomainError, os.str(), loc, fn, block);
        return res;
    }
    if (!validateArgumentCount(*desc, name, args, loc, fn, block))
        return res;

    const auto sigId = il::runtime::findRuntimeSignatureId(name);
    res = dispatchThunk(*desc, args, loc, fn, block, ctx, sigId);
    return res;
}

/// @brief Report a trap originating from the C runtime.
/// @details Invoked by `vm_trap` when a runtime builtin signals a fatal
/// condition. Formats the message with optional function, block, and source
/// location before forwarding it to `rt_abort`.
/// @param kind  Classification of the trap condition.
/// @param msg   Description of the trap condition.
/// @param loc   Source location of the trapping instruction, if available.
/// @param fn    Fully qualified function name containing the call.
/// @param block Label of the basic block with the trapping call.
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

    constexpr OpCode trapOpcode = OpCode::Trap;
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
