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
#include "vm/DiagFormat.hpp"
#include "vm/Marshal.hpp"
#include "vm/OpcodeHandlerHelpers.hpp"
#include "vm/TrapInvariants.hpp"
#include "vm/VM.hpp"

#include <array>
#include <mutex>
#include <optional>
#include <span>
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
///          offending call site. Uses centralized marshalling validation helper.
///
/// @param desc Runtime descriptor describing the callee signature.
/// @param args Slots supplied by the VM as call arguments.
/// @param loc Source location associated with the call.
/// @param fn Name of the function executing the call.
/// @param block Name of the basic block executing the call.
/// @return True when counts match; false when a trap was raised.
static bool validateArgumentCount(const RuntimeDescriptor &desc,
                                  std::span<const Slot> args,
                                  const SourceLoc &loc,
                                  const std::string &fn,
                                  const std::string &block)
{
    auto validation = il::vm::validateMarshalArity(desc, args.size());
    if (validation.ok)
        return true;

    RuntimeBridge::trap(TrapKind::DomainError, validation.errorMessage, loc, fn, block);
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

    // Use stack-allocated marshalling buffer for small argument counts (HIGH-6)
    il::vm::PowStatus powStatus{};
    il::vm::MarshalledArgs marshalledArgs{};
    il::vm::marshalArgumentsInline(desc.signature, argSpan, powStatus, marshalledArgs);

    ResultBuffers buffers{};
    void *resultPtr = il::vm::resultBufferFor(desc.signature.retType.kind, buffers);
    desc.handler(marshalledArgs.empty() ? nullptr : marshalledArgs.data(), resultPtr);

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
///
/// INVARIANT: If ctx.vm is non-null, VM::activeInstance() must also be non-null.
/// GUARANTEE: This function does not return to its caller when no handler catches.
static void finalizeTrap(TrapCtx &ctx)
{
    if (ctx.vm)
    {
        // Assert that activeInstance is consistent with ctx.vm
        VIPER_TRAP_ASSERT(RuntimeBridge::hasActiveVm(),
                          "ActiveVMGuard inconsistency: ctx.vm set but no active VM");
        vm_raise(ctx.kind);
        // vm_raise either throws TrapDispatchSignal or calls rt_abort; it does not return
        // If we reach here, something is wrong
        VIPER_TRAP_ASSERT(false, "vm_raise returned unexpectedly");
        return;
    }

    std::string diagnostic = vm_format_error(ctx.error, ctx.frame);
    if (!ctx.message.empty())
    {
        diagnostic += ": ";
        diagnostic += ctx.message;
    }
    rt_abort(diagnostic.c_str());
    // rt_abort does not return
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
#if defined(_WIN32)
// On Windows, vm_trap is provided by viper_runtime.lib via alternatename.
// Tests can define their own vm_trap to override the default.
// We don't define vm_trap here to avoid duplicate symbol errors with lld-link.
#elif defined(__GNUC__) || defined(__clang__)
/// @brief Weak hook allowing embedders to override VM trap behaviour.
extern "C" __attribute__((weak)) void vm_trap(const char *msg)
{
    const auto *ctx = il::vm::RuntimeBridge::activeContext();
    const char *trapMsg = msg ? msg : "trap";
    if (ctx)
    {
        il::vm::RuntimeBridge::trap(
            TrapKind::DomainError, trapMsg, ctx->loc, ctx->function, ctx->block);
        return;
    }
    if (trapMsg && *trapMsg)
        std::fprintf(stderr, "%s\n", trapMsg);
    std::_Exit(1);
}
#else
/// @brief Default implementation that records traps on the active context.
extern "C" void vm_trap(const char *msg)
{
    const auto *ctx = il::vm::RuntimeBridge::activeContext();
    const char *trapMsg = msg ? msg : "trap";
    if (ctx)
    {
        il::vm::RuntimeBridge::trap(
            TrapKind::DomainError, trapMsg, ctx->loc, ctx->function, ctx->block);
        return;
    }
    if (trapMsg && *trapMsg)
        std::fprintf(stderr, "%s\n", trapMsg);
    std::_Exit(1);
}
#endif

namespace il::vm
{

//===----------------------------------------------------------------------===//
// ExternRegistry Implementation
//===----------------------------------------------------------------------===//
//
// DESIGN NOTE: Extern Registry Scoping
// =====================================
//
// The extern registry supports two modes of operation:
//
// 1. PROCESS-GLOBAL REGISTRY (default):
//    A singleton registry protected by a mutex. All VM instances without a
//    per-VM registry share this global registry. Functions registered via
//    RuntimeBridge::registerExtern() go here.
//
// 2. PER-VM REGISTRY (opt-in):
//    Each VM can optionally hold a pointer to its own ExternRegistry. When
//    resolving extern calls via currentExternRegistry(), the active VM's
//    registry is checked first; if no match is found (or no per-VM registry
//    is configured), the process-global registry is consulted.
//
// Thread Safety:
// - The process-global registry is protected by an internal mutex.
// - Per-VM registries are NOT mutex-protected; they rely on the VM's single-
//   threaded execution model. Embedders must not modify a per-VM registry
//   from another thread while the VM is executing.
//
// Usage Pattern for Per-VM Registries:
//   auto reg = createExternRegistry();      // Create isolated registry
//   vm.setExternRegistry(reg.get());        // Assign to VM (non-owning)
//   registerExternIn(*reg, myExternDesc);   // Populate
//   // ... vm.run() ...
//   // reg must outlive vm
//
//===----------------------------------------------------------------------===//

namespace
{

/// @brief Internal record for a registered external function.
struct ExtRecord
{
    ExternDesc pub;                                ///< Public descriptor exposed to callers.
    il::runtime::RuntimeSignature runtimeSig;      ///< Converted runtime signature.
    il::runtime::RuntimeHandler handler = nullptr; ///< Native handler function.
};

} // namespace

/// @brief Concrete implementation of the ExternRegistry abstraction.
/// @details This struct holds the actual storage (map + mutex) for external
///          function registrations. It is intentionally defined in the .cpp
///          file to keep the header opaque.
struct ExternRegistry
{
    std::mutex mutex;                                   ///< Protects concurrent access.
    std::unordered_map<std::string, ExtRecord> entries; ///< Name -> record mapping.
    bool strictMode = false; ///< When true, reject re-registration with different signature.
};

namespace
{

/// @brief Access the process-global extern registry singleton.
/// @return Reference to the lazily-initialized global registry.
ExternRegistry &globalRegistry()
{
    static ExternRegistry instance;
    return instance;
}

/// @brief Compare two signatures for structural equality.
/// @details Two signatures are equal if they have the same parameter kinds
///          and return kinds in the same order. The name and attribute flags
///          (nothrow, readonly, pure) are ignored for this comparison.
/// @param a First signature.
/// @param b Second signature.
/// @return True if the signatures are structurally equivalent.
static bool signaturesEqual(const Signature &a, const Signature &b)
{
    if (a.params.size() != b.params.size())
        return false;
    if (a.rets.size() != b.rets.size())
        return false;
    for (size_t i = 0; i < a.params.size(); ++i)
    {
        if (a.params[i].kind != b.params[i].kind)
            return false;
    }
    for (size_t i = 0; i < a.rets.size(); ++i)
    {
        if (a.rets[i].kind != b.rets[i].kind)
            return false;
    }
    return true;
}

static il::core::Type mapKind(il::runtime::signatures::SigParam::Kind k)
{
    using K = il::runtime::signatures::SigParam::Kind;
    using il::core::Type;
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

static il::runtime::RuntimeSignature toRuntimeSig(const Signature &sig)
{
    il::runtime::RuntimeSignature rs;
    rs.paramTypes.reserve(sig.params.size());
    for (const auto &p : sig.params)
        rs.paramTypes.push_back(mapKind(p.kind));
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

std::string canonicalizeExternName(std::string_view n)
{
    std::string out(n);
    for (auto &ch : out)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

static const RuntimeDescriptor *resolveRuntimeDescriptor(std::string_view name,
                                                         RuntimeDescriptor &localDesc);
static Slot dispatchRuntimeCall(RuntimeCallContext &ctx,
                                std::string_view name,
                                const RuntimeDescriptor &desc,
                                VM *activeVm);

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
                         std::string_view name,
                         std::span<const Slot> args,
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
    // Use the current context's registry (currently always process-global).
    il::runtime::RuntimeDescriptor localDesc;
    const il::runtime::RuntimeDescriptor *desc = resolveRuntimeDescriptor(name, localDesc);
    if (!desc)
    {
        RuntimeBridge::trap(
            TrapKind::DomainError, diag::formatUnknownRuntimeHelper(name), loc, fn, block);
        return result;
    }
    if (!validateArgumentCount(*desc, args, loc, fn, block))
        return result;

    ctx.descriptor = desc;
    ctx.argBegin = args.empty() ? nullptr : const_cast<Slot *>(args.data());
    ctx.argCount = args.size();

    VM *activeVm = VM::activeInstance();
    result = dispatchRuntimeCall(ctx, name, *desc, activeVm);

    return result;
}

static const RuntimeDescriptor *resolveRuntimeDescriptor(std::string_view name,
                                                         RuntimeDescriptor &localDesc)
{
    il::runtime::RuntimeSignature sig;
    il::runtime::RuntimeHandler handler = nullptr;
    const ExternDesc *extDesc =
        il::vm::resolveExternIn(il::vm::currentExternRegistry(), name, &sig, &handler);
    if (extDesc)
    {
        localDesc.name = extDesc->name;
        localDesc.signature = sig;
        localDesc.handler = handler;
        localDesc.lowering = {};
        return &localDesc;
    }

    return il::runtime::findRuntimeDescriptor(name);
}

static Slot dispatchRuntimeCall(RuntimeCallContext &ctx,
                                std::string_view name,
                                const RuntimeDescriptor &desc,
                                VM *activeVm)
{
    if (activeVm)
    {
        FrameInfo frame{};
        std::optional<RtSig> sigId = il::runtime::findRuntimeSignatureId(name);
        Thunk thunk = nullptr;
        if (sigId && static_cast<std::size_t>(*sigId) < thunkTable().size())
            thunk = thunkTable()[static_cast<std::size_t>(*sigId)];
        if (!thunk)
            thunk = &genericThunk;
        return thunk(*activeVm, frame, ctx);
    }

    return executeDescriptor(desc, ctx.argBegin, ctx.argCount, ctx);
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
        auto populateVm =
            [](VM &vm, const SourceLoc &loc, const std::string &fn, const std::string &block)
        {
            if (loc.hasFile())
            {
                vm.currentContext.loc = loc;
                vm.runtimeContext.loc = loc;
            }
            else
            {
                vm.runtimeContext.loc = {};
            }
            if (!fn.empty())
            {
                vm.runtimeContext.function = fn;
            }
            else
            {
                vm.runtimeContext.function.clear();
                vm.lastTrap.frame.function.clear();
            }
            if (!block.empty())
            {
                vm.runtimeContext.block = block;
            }
            else
            {
                vm.runtimeContext.block.clear();
            }
            if (!loc.hasLine())
                vm.lastTrap.frame.line = -1;
        };
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
        populateNoVm(ctx, kind, loc, fn);
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

bool RuntimeBridge::hasActiveVm()
{
    return VM::activeInstance() != nullptr;
}

ExternRegistry *RuntimeBridge::activeVmRegistry()
{
    if (VM *vm = VM::activeInstance())
        return vm->externRegistry();
    return nullptr;
}

Slot RuntimeBridge::call(RuntimeCallContext &ctx,
                         std::string_view name,
                         const std::vector<Slot> &args,
                         const SourceLoc &loc,
                         const std::string &fn,
                         const std::string &block)
{
    return RuntimeBridge::call(
        ctx, name, std::span<const Slot>{args.data(), args.size()}, loc, fn, block);
}

Slot RuntimeBridge::call(RuntimeCallContext &ctx,
                         std::string_view name,
                         std::initializer_list<Slot> args,
                         const SourceLoc &loc,
                         const std::string &fn,
                         const std::string &block)
{
    return RuntimeBridge::call(
        ctx, name, std::span<const Slot>{args.begin(), args.size()}, loc, fn, block);
}

//===----------------------------------------------------------------------===//
// ExternRegistry Free Functions
//===----------------------------------------------------------------------===//

ExternRegistry &processGlobalExternRegistry()
{
    return globalRegistry();
}

ExternRegistry &currentExternRegistry()
{
    // Check for active VM with a per-VM registry configured.
    // Falls back to the process-global registry when:
    // - No VM is currently active, or
    // - The active VM has no per-VM registry assigned.
    if (ExternRegistry *reg = RuntimeBridge::activeVmRegistry())
        return *reg;
    return globalRegistry();
}

ExternRegisterResult registerExternIn(ExternRegistry &registry, const ExternDesc &ext)
{
    ExtRecord rec;
    rec.pub = ext;
    rec.runtimeSig = toRuntimeSig(ext.signature);
    rec.handler = reinterpret_cast<il::runtime::RuntimeHandler>(ext.fn);
    const std::string key = canonicalizeExternName(ext.name);
    std::lock_guard<std::mutex> lock(registry.mutex);

    // Check for existing entry with same name
    auto it = registry.entries.find(key);
    if (it != registry.entries.end())
    {
        // Already registered - check signature compatibility
        if (signaturesEqual(it->second.pub.signature, ext.signature))
        {
            // Same signature: update silently (no-op if fn is also the same)
            it->second = std::move(rec);
            return ExternRegisterResult::Success;
        }
        else
        {
            // Different signature: error in strict mode, warning otherwise
            if (registry.strictMode)
            {
                return ExternRegisterResult::SignatureMismatch;
            }
            // Non-strict mode: overwrite and continue
            it->second = std::move(rec);
            return ExternRegisterResult::Success;
        }
    }

    // New registration
    registry.entries.emplace(key, std::move(rec));
    return ExternRegisterResult::Success;
}

bool unregisterExternIn(ExternRegistry &registry, std::string_view name)
{
    const std::string key = canonicalizeExternName(name);
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.entries.erase(key) > 0;
}

const ExternDesc *findExternIn(ExternRegistry &registry, std::string_view name)
{
    const std::string key = canonicalizeExternName(name);
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto it = registry.entries.find(key);
    if (it == registry.entries.end())
        return nullptr;
    return &it->second.pub;
}

const ExternDesc *resolveExternIn(ExternRegistry &registry,
                                  std::string_view name,
                                  il::runtime::RuntimeSignature *outSig,
                                  il::runtime::RuntimeHandler *outHandler)
{
    const std::string key = canonicalizeExternName(name);
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto it = registry.entries.find(key);
    if (it == registry.entries.end())
        return nullptr;
    if (outSig)
        *outSig = it->second.runtimeSig;
    if (outHandler)
        *outHandler = it->second.handler;
    return &it->second.pub;
}

//===----------------------------------------------------------------------===//
// ExternRegistry Strict Mode API
//===----------------------------------------------------------------------===//

void setExternRegistryStrictMode(ExternRegistry &registry, bool enabled)
{
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.strictMode = enabled;
}

bool isExternRegistryStrictMode(const ExternRegistry &registry)
{
    // Note: reading a bool is atomic on all supported platforms, but we lock
    // for consistency with the setter and to be future-proof.
    std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(registry.mutex));
    return registry.strictMode;
}

//===----------------------------------------------------------------------===//
// ExternRegistry Factory and Deleter
//===----------------------------------------------------------------------===//

void ExternRegistryDeleter::operator()(ExternRegistry *reg) const noexcept
{
    delete reg;
}

ExternRegistryPtr createExternRegistry()
{
    return ExternRegistryPtr(new ExternRegistry());
}

//===----------------------------------------------------------------------===//
// RuntimeBridge Static Methods (Delegate to Process-Global Registry)
//===----------------------------------------------------------------------===//

void RuntimeBridge::registerExtern(const ExternDesc &ext)
{
    registerExternIn(processGlobalExternRegistry(), ext);
}

bool RuntimeBridge::unregisterExtern(std::string_view name)
{
    return unregisterExternIn(processGlobalExternRegistry(), name);
}

const ExternDesc *RuntimeBridge::findExtern(std::string_view name)
{
    return findExternIn(processGlobalExternRegistry(), name);
}

} // namespace il::vm
