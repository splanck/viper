//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/RuntimeBridge.cpp
// Purpose: Provide the glue between the Viper VM and the C runtime library.
// Key invariants: The bridge maintains thread-local trap context and validates
//                 runtime signatures before invocation.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the runtime bridge that dispatches IL runtime calls.
/// @details The bridge validates call arity, marshals VM slots into native
///          representations, invokes runtime thunks, and translates traps back
///          into VM errors.  It also exposes entry points used by the C runtime
///          to signal asynchronous traps into the active VM context.

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
using il::runtime::RtSig;
using il::runtime::RuntimeDescriptor;
using il::vm::FrameInfo;
using il::vm::ResultBuffers;
using il::vm::RuntimeBridge;
using il::vm::RuntimeCallContext;
using il::vm::Slot;
using il::vm::TrapKind;
using il::vm::VM;
using il::vm::vm_format_error;
using il::vm::vm_raise;
using il::vm::VmError;

/// @brief Thread-local pointer to the runtime call context for active trap reporting.
///
/// The bridge stores the most recent call's context so asynchronous traps raised
/// from the C runtime can report diagnostics against the correct function and
/// source location.  The pointer is managed via @ref ContextGuard to ensure
/// balanced updates.
thread_local RuntimeCallContext *tlsContext = nullptr;

using VmResult = Slot;
using Thunk = VmResult (*)(VM &, FrameInfo &, const RuntimeCallContext &);

/// @brief Verify that a runtime call supplies the expected number of arguments.
///
/// @details Compares the descriptor's signature against the arguments assembled
///          by the VM.  Mismatches trigger a domain-error trap describing the
///          offending call site.
///
/// @param desc Runtime descriptor describing the callee signature.
/// @param name Human-readable name for diagnostics (e.g., "runtime call").
/// @param args Slots supplied by the VM as call arguments.
/// @param loc Source location associated with the call.
/// @param fn Name of the function executing the call.
/// @param block Name of the basic block executing the call.
/// @return True when counts match; false when a trap was raised.
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

/// @brief Execute a runtime descriptor by marshalling arguments and collecting results.
///
/// @details Converts VM slot arguments into the ABI expected by the runtime
///          library, allocates temporary buffers for return values, invokes the
///          descriptor's handler, and translates any power-trap metadata into VM
///          traps.
///
/// @param desc Runtime descriptor to invoke.
/// @param argBegin Pointer to the first argument slot (may be null when @p argCount is zero).
/// @param argCount Number of argument slots provided.
/// @param ctx Call context providing trap location metadata.
/// @return Slot containing the marshalled return value.
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

/// @brief Generic thunk that executes descriptors without VM-specific side effects.
///
/// @details The VM and frame parameters are unused for most runtime functions;
///          they are present to match the signature expected by the thunk table.
static VmResult genericThunk(VM &vm, FrameInfo &frame, const RuntimeCallContext &ctx)
{
    (void)vm;
    (void)frame;
    return executeDescriptor(*ctx.descriptor, ctx.argBegin, ctx.argCount, ctx);
}

/// @brief Construct the table of thunks indexed by runtime signature tags.
///
/// @details Each entry defaults to the generic thunk for now, but the table is
///          built as a constexpr helper so future specialised thunks can be
///          registered in one place.
constexpr std::array<Thunk, static_cast<std::size_t>(RtSig::Count)> buildThunkTable()
{
    std::array<Thunk, static_cast<std::size_t>(RtSig::Count)> table{};
    table.fill(&genericThunk);
    return table;
}

/// @brief Access the lazily initialised thunk table.
///
/// @return Reference to the singleton thunk array used for runtime dispatch.
const std::array<Thunk, static_cast<std::size_t>(RtSig::Count)> &thunkTable()
{
    static const auto table = buildThunkTable();
    return table;
}

/// @brief RAII helper that installs a runtime call context for the current thread.
struct ContextGuard
{
    RuntimeCallContext *previous;
    RuntimeCallContext *current;

    /// @brief Push the provided context as the thread-local active call.
    explicit ContextGuard(RuntimeCallContext &ctx) : previous(tlsContext), current(&ctx)
    {
        tlsContext = &ctx;
    }

    /// @brief Restore the previous context and clear transient diagnostic fields.
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

/// @brief Aggregates information required to finalise a runtime trap.
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

/// @brief Deliver a trap either to the active VM or to the call-site context.
///
/// @details When a VM is executing the trap escalates through @ref vm_raise.
///          Otherwise the trap information is recorded directly on the context
///          so higher layers can surface it to the user.
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

/// @brief Populate overflow-specific diagnostics prior to finalising a trap.
///
/// @param ctx Aggregated trap context to populate.
/// @param opcode Opcode that triggered the overflow.
/// @param operands Operands involved in the failing operation.
static void handleOverflow(TrapCtx &ctx, Opcode opcode, const Operands &operands)
{
    (void)opcode;
    (void)operands;
    finalizeTrap(ctx);
}

/// @brief Populate divide-by-zero diagnostics prior to finalising a trap.
///
/// @param ctx Aggregated trap context to populate.
/// @param opcode Opcode that triggered the trap.
/// @param operands Operands supplied to the operation.
static void handleDivByZero(TrapCtx &ctx, Opcode opcode, const Operands &operands)
{
    (void)opcode;
    (void)operands;
    finalizeTrap(ctx);
}

/// @brief Finalise traps that do not require operand-specific formatting.
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
/// @brief Weak hook allowing embedders to override VM trap behaviour.
extern "C" __attribute__((weak)) void vm_trap(const char *msg)
#else
/// @brief Default implementation that records traps on the active context.
extern "C" void vm_trap(const char *msg)
#endif
{
    const auto *ctx = il::vm::RuntimeBridge::activeContext();
    const char *trapMsg = msg ? msg : "trap";
    if (ctx)
        il::vm::RuntimeBridge::trap(
            TrapKind::DomainError, trapMsg, ctx->loc, ctx->function, ctx->block);
    else
        il::vm::RuntimeBridge::trap(TrapKind::DomainError, trapMsg, {}, "", "");
}

namespace il::vm
{

/// @brief Invoke a runtime helper identified by name on behalf of the VM.
///
/// @details Validates the callee descriptor, checks argument counts, installs
///          the call context for trap reporting, and dispatches through the thunk
///          table or directly when no VM is active.  On failure the function
///          records diagnostics and returns a zero-initialised slot.
///
/// @param ctx Mutable call context tracking diagnostics and temporary buffers.
/// @param name Runtime helper symbol to resolve.
/// @param args Argument slots supplied by the VM.
/// @param loc Source location associated with the call site.
/// @param fn Function name executing the call.
/// @param block Block label executing the call.
/// @return Slot containing the runtime result or zero on trap.
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

/// @brief Record a runtime trap and escalate it to the VM when applicable.
///
/// @details Populates a @ref TrapCtx structure with diagnostic metadata and
///          delegates to specialised helpers based on @p kind before finalising
///          delivery via @ref finalizeTrap.
///
/// @param kind Kind of trap raised by the runtime.
/// @param msg Human-readable diagnostic payload.
/// @param loc Source location associated with the trap.
/// @param fn Function name active when the trap occurred.
/// @param block Block label active when the trap occurred.
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
        if (loc.hasFile())
        {
            ctx.vm->currentContext.loc = loc;
            ctx.vm->runtimeContext.loc = loc;
        }
        else
        {
            ctx.vm->runtimeContext.loc = {};
        }
        if (!fn.empty())
        {
            ctx.vm->runtimeContext.function = fn;
        }
        else
        {
            ctx.vm->runtimeContext.function.clear();
            ctx.vm->lastTrap.frame.function.clear();
        }
        if (!block.empty())
        {
            ctx.vm->runtimeContext.block = block;
        }
        else
        {
            ctx.vm->runtimeContext.block.clear();
        }
        if (!loc.hasLine())
            ctx.vm->lastTrap.frame.line = -1;
        ctx.vm->runtimeContext.message = msg;
    }
    else
    {
        ctx.error.kind = kind;
        ctx.error.code = 0;
        ctx.error.ip = 0;
        ctx.error.line = loc.hasLine() ? static_cast<int32_t>(loc.line) : -1;

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

/// @brief Retrieve the currently installed runtime call context, if any.
///
/// @return Pointer to the context managed by @ref ContextGuard, or @c nullptr when inactive.
const RuntimeCallContext *RuntimeBridge::activeContext()
{
    return tlsContext;
}

} // namespace il::vm
