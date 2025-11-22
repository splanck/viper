//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "vm/OpcodeHandlerHelpers.hpp"
#include "vm/VM.hpp"

#include <array>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using il::support::SourceLoc;

namespace
{
using il::core::Opcode;
using il::runtime::RtSig;
using il::runtime::RuntimeDescriptor;
using il::vm::ExternDesc;
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
///          offending call site. Uses shared error formatting helper for consistency.
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
/// @brief Implements if functionality.
/// @param args.size( Parameter description needed.
/// @return Return value description needed.
    if (args.size() == expected)
        return true;

    const std::string message =
        il::vm::detail::formatArgumentCountError(name, expected, args.size());
    RuntimeBridge::trap(TrapKind::DomainError, message, loc, fn, block);
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
/// @brief Implements if functionality.
/// @param argCount Parameter description needed.
/// @return Return value description needed.
    if (argBegin && argCount)
        argSpan = {argBegin, argCount};

    il::vm::PowStatus powStatus{};
    auto rawArgs = il::vm::marshalArguments(desc.signature, argSpan, powStatus);

    ResultBuffers buffers{};
    void *resultPtr = il::vm::resultBufferFor(desc.signature.retType.kind, buffers);
    desc.handler(rawArgs.empty() ? nullptr : rawArgs.data(), resultPtr);

    std::span<const Slot> readonlyArgs{};
/// @brief Implements if functionality.
/// @param argCount Parameter description needed.
/// @return Return value description needed.
    if (argBegin && argCount)
        readonlyArgs = {argBegin, argCount};
    auto trap = il::vm::classifyPowTrap(desc, powStatus, readonlyArgs, buffers);
/// @brief Implements if functionality.
/// @param trap.triggered Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param current Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param ctx.vm Parameter description needed.
/// @return Return value description needed.
    if (ctx.vm)
    {
/// @brief Implements vm_raise functionality.
/// @param ctx.kind Parameter description needed.
/// @return Return value description needed.
        vm_raise(ctx.kind);
        return;
    }

    std::string diagnostic = vm_format_error(ctx.error, ctx.frame);
/// @brief Implements if functionality.
/// @param !ctx.message.empty( Parameter description needed.
/// @return Return value description needed.
    if (!ctx.message.empty())
    {
        diagnostic += ": ";
        diagnostic += ctx.message;
    }
/// @brief Implements rt_abort functionality.
/// @param diagnostic.c_str( Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements finalizeTrap functionality.
/// @param ctx Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements finalizeTrap functionality.
/// @param ctx Parameter description needed.
/// @return Return value description needed.
    finalizeTrap(ctx);
}

/// @brief Finalise traps that do not require operand-specific formatting.
static void handleGenericTrap(TrapCtx &ctx)
{
/// @brief Implements finalizeTrap functionality.
/// @param ctx Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param ctx Parameter description needed.
/// @return Return value description needed.
    if (ctx)
    {
        il::vm::RuntimeBridge::trap(
            TrapKind::DomainError, trapMsg, ctx->loc, ctx->function, ctx->block);
        return;
    }
    // No active runtime call context. Avoid re-entering RuntimeBridge::trap which
    // would attempt to format and route a second trap and can cause recursion when
    // the formatting path calls back into rt_abort/vm_trap. Instead, emit a minimal
    // diagnostic and terminate the process with a non-zero status so tests that
    // expect failure (via fork) observe a failing exit code.
/// @brief Implements if functionality.
/// @param trapMsg Parameter description needed.
/// @return Return value description needed.
    if (trapMsg && *trapMsg)
        std::fprintf(stderr, "%s\n", trapMsg);
    std::_Exit(1);
}

namespace il::vm
{
namespace
{
struct ExtRecord
{
    ExternDesc pub;
    il::runtime::RuntimeSignature runtimeSig;
    il::runtime::RuntimeHandler handler = nullptr;
};

std::mutex &externMutex()
{
    static std::mutex mtx;
    return mtx;
}

std::unordered_map<std::string, ExtRecord> &externRegistry()
{
    static std::unordered_map<std::string, ExtRecord> reg;
    return reg;
}

/// @brief Implements mapKind functionality.
/// @param k Parameter description needed.
/// @return Return value description needed.
static il::core::Type mapKind(il::runtime::signatures::SigParam::Kind k)
{
    using K = il::runtime::signatures::SigParam::Kind;
    using il::core::Type;
/// @brief Implements switch functionality.
/// @param k Parameter description needed.
/// @return Return value description needed.
    switch (k)
    {
        case K::I1:
            return Type(Type::Kind::I1);
        case K::I32:
            return Type(Type::Kind::I32);
        case K::I64:
            return Type(Type::Kind::I64);
        case K::F32:
            return Type(Type::Kind::F64);
        case K::F64:
            return Type(Type::Kind::F64);
        case K::Ptr:
            return Type(Type::Kind::Ptr);
        case K::Str:
            return Type(Type::Kind::Ptr);
    }
    return Type(Type::Kind::Void);
}

/// @brief Implements toRuntimeSig functionality.
/// @param sig Parameter description needed.
/// @return Return value description needed.
static il::runtime::RuntimeSignature toRuntimeSig(const Signature &sig)
{
    il::runtime::RuntimeSignature rs;
    rs.paramTypes.reserve(sig.params.size());
/// @brief Implements for functionality.
/// @param sig.params Parameter description needed.
/// @return Return value description needed.
    for (const auto &p : sig.params)
        rs.paramTypes.push_back(mapKind(p.kind));
/// @brief Implements if functionality.
/// @param !sig.rets.empty( Parameter description needed.
/// @return Return value description needed.
    if (!sig.rets.empty())
        rs.retType = mapKind(sig.rets.front().kind);
    else
        rs.retType = il::core::Type(il::core::Type::Kind::Void);
    rs.trapClass = il::runtime::RuntimeTrapClass::None;
    rs.nothrow = sig.nothrow;
    rs.readonly = sig.readonly;
    rs.pure = sig.pure;
    return rs;
}
} // namespace

/// @brief Implements canonicalizeExternName functionality.
/// @param n Parameter description needed.
/// @return Return value description needed.
std::string canonicalizeExternName(std::string_view n)
{
/// @brief Implements out functionality.
/// @param n Parameter description needed.
/// @return Return value description needed.
    std::string out(n);
/// @brief Implements for functionality.
/// @param out Parameter description needed.
/// @return Return value description needed.
    for (auto &ch : out)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

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

    // Resolve against runtime extern registry first, then built-ins.
    il::runtime::RuntimeDescriptor localDesc;
    const il::runtime::RuntimeDescriptor *desc = nullptr;
    {
        std::unique_lock<std::mutex> lock(externMutex());
        auto it = externRegistry().find(canonicalizeExternName(name));
/// @brief Implements if functionality.
/// @param externRegistry( Parameter description needed.
/// @return Return value description needed.
        if (it != externRegistry().end())
        {
            localDesc.name = it->second.pub.name;
            localDesc.signature = it->second.runtimeSig;
            localDesc.handler = it->second.handler;
            localDesc.lowering = {};
            desc = &localDesc;
        }
    }
/// @brief Implements if functionality.
/// @param !desc Parameter description needed.
/// @return Return value description needed.
    if (!desc)
        desc = il::runtime::findRuntimeDescriptor(name);
/// @brief Implements if functionality.
/// @param !desc Parameter description needed.
/// @return Return value description needed.
    if (!desc)
    {
        std::ostringstream os;
        os << "attempted to call unknown runtime helper '" << name << '\'';
        RuntimeBridge::trap(TrapKind::DomainError, os.str(), loc, fn, block);
        return result;
    }
/// @brief Implements if functionality.
/// @param !validateArgumentCount(*desc Parameter description needed.
/// @param name Parameter description needed.
/// @param args Parameter description needed.
/// @param loc Parameter description needed.
/// @param fn Parameter description needed.
/// @param block Parameter description needed.
/// @return Return value description needed.
    if (!validateArgumentCount(*desc, name, args, loc, fn, block))
        return result;

    ctx.descriptor = desc;
    ctx.argBegin = args.empty() ? nullptr : const_cast<Slot *>(args.data());
    ctx.argCount = args.size();

/// @brief Implements if functionality.
/// @param VM::activeInstance( Parameter description needed.
/// @return Return value description needed.
    if (auto *vm = VM::activeInstance())
    {
        FrameInfo frame{};
        std::optional<RtSig> sigId = il::runtime::findRuntimeSignatureId(name);
        Thunk thunk = nullptr;
/// @brief Implements if functionality.
/// @param static_cast<std::size_t>(*sigId Parameter description needed.
/// @return Return value description needed.
        if (sigId && static_cast<std::size_t>(*sigId) < thunkTable().size())
            thunk = thunkTable()[static_cast<std::size_t>(*sigId)];
/// @brief Implements if functionality.
/// @param !thunk Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param ctx.vm Parameter description needed.
/// @return Return value description needed.
    if (ctx.vm)
    {
        auto populateVm =
            [](VM &vm, const SourceLoc &loc, const std::string &fn, const std::string &block)
        {
/// @brief Implements if functionality.
/// @param loc.hasFile( Parameter description needed.
/// @return Return value description needed.
            if (loc.hasFile())
            {
                vm.currentContext.loc = loc;
                vm.runtimeContext.loc = loc;
            }
            else
            {
                vm.runtimeContext.loc = {};
            }
/// @brief Implements if functionality.
/// @param !fn.empty( Parameter description needed.
/// @return Return value description needed.
            if (!fn.empty())
            {
                vm.runtimeContext.function = fn;
            }
            else
            {
                vm.runtimeContext.function.clear();
                vm.lastTrap.frame.function.clear();
            }
/// @brief Implements if functionality.
/// @param !block.empty( Parameter description needed.
/// @return Return value description needed.
            if (!block.empty())
            {
                vm.runtimeContext.block = block;
            }
            else
            {
                vm.runtimeContext.block.clear();
            }
/// @brief Implements if functionality.
/// @param !loc.hasLine( Parameter description needed.
/// @return Return value description needed.
            if (!loc.hasLine())
                vm.lastTrap.frame.line = -1;
        };
/// @brief Implements populateVm functionality.
/// @param ctx.vm Parameter description needed.
/// @param loc Parameter description needed.
/// @param fn Parameter description needed.
/// @param block Parameter description needed.
/// @return Return value description needed.
        populateVm(*ctx.vm, loc, fn, block);
        ctx.vm->runtimeContext.message = msg;
    }
    else
    {
        auto populateNoVm =
            [](TrapCtx &c, TrapKind kind, const SourceLoc &loc, const std::string &fn)
        {
            c.error.kind = kind;
            c.error.code = 0;
            c.error.ip = 0;
            c.error.line = loc.hasLine() ? static_cast<int32_t>(loc.line) : -1;

            c.frame.function = fn.empty() ? std::string("<unknown>") : fn;
            c.frame.ip = 0;
            c.frame.line = c.error.line;
            c.frame.handlerInstalled = false;
        };
/// @brief Implements populateNoVm functionality.
/// @param ctx Parameter description needed.
/// @param kind Parameter description needed.
/// @param loc Parameter description needed.
/// @param fn Parameter description needed.
/// @return Return value description needed.
        populateNoVm(ctx, kind, loc, fn);
    }

    constexpr Opcode trapOpcode = Opcode::Trap;
    const Operands noOperands{};

/// @brief Implements switch functionality.
/// @param kind Parameter description needed.
/// @return Return value description needed.
    switch (kind)
    {
        case TrapKind::Overflow:
/// @brief Implements handleOverflow functionality.
/// @param ctx Parameter description needed.
/// @param trapOpcode Parameter description needed.
/// @param noOperands Parameter description needed.
/// @return Return value description needed.
            handleOverflow(ctx, trapOpcode, noOperands);
            return;
        case TrapKind::DivideByZero:
/// @brief Implements handleDivByZero functionality.
/// @param ctx Parameter description needed.
/// @param trapOpcode Parameter description needed.
/// @param noOperands Parameter description needed.
/// @return Return value description needed.
            handleDivByZero(ctx, trapOpcode, noOperands);
            return;
        default:
/// @brief Implements handleGenericTrap functionality.
/// @param ctx Parameter description needed.
/// @return Return value description needed.
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

bool RuntimeBridge::hasActiveVm()
{
    return VM::activeInstance() != nullptr;
}

void RuntimeBridge::registerExtern(const ExternDesc &ext)
{
    ExtRecord rec;
    rec.pub = ext;
    rec.runtimeSig = toRuntimeSig(ext.signature);
    rec.handler = reinterpret_cast<il::runtime::RuntimeHandler>(ext.fn);
    const std::string key = canonicalizeExternName(ext.name);
    std::lock_guard<std::mutex> lock(externMutex());
/// @brief Implements externRegistry functionality.
/// @return Return value description needed.
    externRegistry()[key] = std::move(rec);
}

bool RuntimeBridge::unregisterExtern(std::string_view name)
{
    std::lock_guard<std::mutex> lock(externMutex());
    return externRegistry().erase(canonicalizeExternName(name)) > 0;
}

/// @brief Implements findExtern functionality.
/// @param name Parameter description needed.
/// @return Return value description needed.
const ExternDesc *RuntimeBridge::findExtern(std::string_view name)
{
    std::lock_guard<std::mutex> lock(externMutex());
    auto it = externRegistry().find(canonicalizeExternName(name));
/// @brief Implements if functionality.
/// @param externRegistry( Parameter description needed.
/// @return Return value description needed.
    if (it == externRegistry().end())
        return nullptr;
    return &it->second.pub;
}

} // namespace il::vm
