// File: src/vm/RuntimeBridge.cpp
// Purpose: Bridges VM to C runtime functions.
// License: MIT License
// Key invariants: None.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-guide.md#reference

#include "vm/RuntimeBridge.hpp"
#include "il/core/Opcode.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "vm/VM.hpp"
#include "rt_fp.h"
#include <array>
#include <cassert>
#include <cmath>
#include <span>
#include <sstream>
#include <vector>

using il::support::SourceLoc;

namespace
{
using il::core::kindToString;
using il::core::Opcode;
using il::vm::FrameInfo;
using il::vm::RuntimeBridge;
using il::vm::RuntimeCallContext;
using il::vm::Slot;
using il::vm::TrapKind;
using il::vm::VM;
using il::vm::VmError;
using il::vm::vm_format_error;
using il::vm::vm_raise;
using il::core::Type;
using il::runtime::RuntimeHiddenParamKind;
using il::runtime::RuntimeTrapClass;
using il::runtime::RuntimeDescriptor;
using il::runtime::RuntimeSignature;

/// @brief Thread-local pointer to the runtime call context for active trap reporting.
thread_local RuntimeCallContext *tlsContext = nullptr;

/// @brief Aggregate of temporary storage for marshalled runtime results.
struct ResultBuffers
{
    int64_t i64 = 0;       ///< Storage for integer and boolean results.
    double f64 = 0.0;      ///< Storage for floating-point results.
    rt_string str = nullptr; ///< Storage for runtime string results.
    void *ptr = nullptr;   ///< Storage for pointer results.
};

struct PowStatus
{
    bool active{false};
    bool ok{true};
    bool *ptr{nullptr};
};

/// @brief Table entry describing how a particular @ref Type::Kind maps to Slot and
/// runtime buffer storage.
struct KindAccessors
{
    using SlotAccessor = void *(*)(Slot &);
    using ResultAccessor = void *(*)(ResultBuffers &);
    using ResultAssigner = void (*)(Slot &, const ResultBuffers &);

    SlotAccessor slotAccessor = nullptr;       ///< Accessor for VM argument slots.
    ResultAccessor resultAccessor = nullptr;   ///< Accessor for runtime result buffers.
    ResultAssigner assignResult = nullptr;     ///< Assignment routine for marshalled results.
};

constexpr std::array<Type::Kind, 10> kSupportedKinds = {
    Type::Kind::Void,
    Type::Kind::I1,
    Type::Kind::I16,
    Type::Kind::I32,
    Type::Kind::I64,
    Type::Kind::F64,
    Type::Kind::Ptr,
    Type::Kind::Str,
    Type::Kind::Error,
    Type::Kind::ResumeTok,
};

static_assert(kSupportedKinds.size() == 10, "update kind accessors when Type::Kind grows");

constexpr void *nullResultBuffer(ResultBuffers &)
{
    return nullptr;
}

constexpr void assignNoop(Slot &, const ResultBuffers &)
{
}

template <auto Member>
constexpr void *slotMemberAccessor(Slot &slot)
{
    return static_cast<void *>(&(slot.*Member));
}

template <auto Member>
constexpr void *bufferMemberAccessor(ResultBuffers &buffers)
{
    return static_cast<void *>(&(buffers.*Member));
}

template <auto SlotMember, auto BufferMember>
constexpr void assignFromBuffer(Slot &slot, const ResultBuffers &buffers)
{
    slot.*SlotMember = buffers.*BufferMember;
}

constexpr KindAccessors makeVoidAccessors()
{
    return KindAccessors{nullptr, &nullResultBuffer, &assignNoop};
}

template <auto SlotMember, auto BufferMember>
constexpr KindAccessors makeAccessors()
{
    return KindAccessors{
        &slotMemberAccessor<SlotMember>,
        &bufferMemberAccessor<BufferMember>,
        &assignFromBuffer<SlotMember, BufferMember>,
    };
}

constexpr std::array<KindAccessors, kSupportedKinds.size()> kKindAccessors = [] {
    std::array<KindAccessors, kSupportedKinds.size()> table{};
    table[static_cast<size_t>(Type::Kind::Void)] = makeVoidAccessors();
    table[static_cast<size_t>(Type::Kind::I1)] = makeAccessors<&Slot::i64, &ResultBuffers::i64>();
    table[static_cast<size_t>(Type::Kind::I16)] = makeAccessors<&Slot::i64, &ResultBuffers::i64>();
    table[static_cast<size_t>(Type::Kind::I32)] = makeAccessors<&Slot::i64, &ResultBuffers::i64>();
    table[static_cast<size_t>(Type::Kind::I64)] = makeAccessors<&Slot::i64, &ResultBuffers::i64>();
    table[static_cast<size_t>(Type::Kind::F64)] = makeAccessors<&Slot::f64, &ResultBuffers::f64>();
    table[static_cast<size_t>(Type::Kind::Ptr)] = makeAccessors<&Slot::ptr, &ResultBuffers::ptr>();
    table[static_cast<size_t>(Type::Kind::Str)] = makeAccessors<&Slot::str, &ResultBuffers::str>();
    table[static_cast<size_t>(Type::Kind::Error)] = makeVoidAccessors();
    table[static_cast<size_t>(Type::Kind::ResumeTok)] = makeVoidAccessors();
    return table;
}();

static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::Void)].slotAccessor == nullptr,
              "Void must not expose a slot accessor");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::Void)].resultAccessor == &nullResultBuffer,
              "Void must map to the null result buffer");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::I1)].slotAccessor ==
                  &slotMemberAccessor<&Slot::i64>,
              "I1 slot accessor must target Slot::i64");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::I16)].slotAccessor ==
                  &slotMemberAccessor<&Slot::i64>,
              "I16 slot accessor must target Slot::i64");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::I32)].slotAccessor ==
                  &slotMemberAccessor<&Slot::i64>,
              "I32 slot accessor must target Slot::i64");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::I64)].slotAccessor ==
                  &slotMemberAccessor<&Slot::i64>,
              "I64 slot accessor must target Slot::i64");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::F64)].slotAccessor ==
                  &slotMemberAccessor<&Slot::f64>,
              "F64 slot accessor must target Slot::f64");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::Ptr)].slotAccessor ==
                  &slotMemberAccessor<&Slot::ptr>,
              "Ptr slot accessor must target Slot::ptr");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::Str)].slotAccessor ==
                  &slotMemberAccessor<&Slot::str>,
              "Str slot accessor must target Slot::str");

const KindAccessors &dispatchFor(Type::Kind kind)
{
    const auto index = static_cast<size_t>(kind);
    assert(index < kKindAccessors.size() && "invalid type kind");
    return kKindAccessors[index];
}

void *reportUnsupportedPointer(std::string msg)
{
    RuntimeBridge::trap(TrapKind::InvalidOperation, msg, {}, "", "");
    return nullptr;
}

void reportUnsupportedAssign(std::string msg)
{
    RuntimeBridge::trap(TrapKind::InvalidOperation, msg, {}, "", "");
}

/// @brief Translate a VM slot to the pointer expected by the runtime handler.
/// @param slot Slot containing the argument value.
/// @param kind IL type kind describing the slot contents.
/// @return Pointer to the slot member corresponding to @p kind.
void *slotToArgPointer(Slot &slot, Type::Kind kind)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.slotAccessor)
    {
        std::ostringstream os;
        os << "runtime bridge does not support argument kind '" << kindToString(kind)
           << "'";
        return reportUnsupportedPointer(os.str());
    }
    return entry.slotAccessor(slot);
}

/// @brief Obtain the buffer address to receive a runtime result of @p kind.
/// @param kind Type kind of the runtime return value.
/// @param buffers Temporary storage for return values.
/// @return Pointer handed to the runtime handler for writing the result.
void *resultBufferFor(Type::Kind kind, ResultBuffers &buffers)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.resultAccessor)
    {
        std::ostringstream os;
        os << "runtime bridge does not support return kind '" << kindToString(kind) << "'";
        return reportUnsupportedPointer(os.str());
    }
    return entry.resultAccessor(buffers);
}

/// @brief Store the marshalled runtime result back into VM slot @p slot.
/// @param slot Destination VM slot.
/// @param kind Type kind describing the expected slot member.
/// @param buffers Temporary storage containing the runtime result.
void assignResult(Slot &slot, Type::Kind kind, const ResultBuffers &buffers)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.assignResult)
    {
        std::ostringstream os;
        os << "runtime bridge cannot assign return kind '" << kindToString(kind) << "'";
        reportUnsupportedAssign(os.str());
        return;
    }
    entry.assignResult(slot, buffers);
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

static std::vector<void *> marshalArguments(const RuntimeDescriptor &desc,
                                            const std::vector<Slot> &args,
                                            PowStatus &powStatus)
{
    const RuntimeSignature &sig = desc.signature;
    std::vector<void *> rawArgs(sig.paramTypes.size() + sig.hiddenParams.size());

    for (size_t i = 0; i < sig.paramTypes.size(); ++i)
    {
        auto kind = sig.paramTypes[i].kind;
        Slot &slot = const_cast<Slot &>(args[i]);
        rawArgs[i] = slotToArgPointer(slot, kind);
    }

    size_t hiddenIndex = sig.paramTypes.size();
    for (const auto &hidden : sig.hiddenParams)
    {
        switch (hidden.kind)
        {
        case RuntimeHiddenParamKind::None:
            rawArgs[hiddenIndex++] = nullptr;
            break;
        case RuntimeHiddenParamKind::PowStatusPointer:
            powStatus.active = true;
            powStatus.ok = true;
            powStatus.ptr = &powStatus.ok;
            rawArgs[hiddenIndex++] = &powStatus.ptr;
            break;
        }
    }

    return rawArgs;
}

static bool handlePowTrap(const RuntimeDescriptor &desc,
                          const PowStatus &powStatus,
                          const std::vector<Slot> &args,
                          const ResultBuffers &buffers,
                          const SourceLoc &loc,
                          const std::string &fn,
                          const std::string &block)
{
    if (desc.trapClass != RuntimeTrapClass::PowDomainOverflow || !powStatus.active)
        return false;

    const RuntimeSignature &sig = desc.signature;

    if (!powStatus.ok)
    {
        const double base = !args.empty() ? args[0].f64 : 0.0;
        const double exp = (args.size() > 1) ? args[1].f64 : 0.0;
        const bool expIntegral = std::isfinite(exp) && (exp == std::trunc(exp));
        const bool domainError = (base < 0.0) && !expIntegral;

        std::ostringstream os;
        if (domainError)
        {
            os << "rt_pow_f64_chkdom: negative base with fractional exponent";
            RuntimeBridge::trap(TrapKind::DomainError, os.str(), loc, fn, block);
        }
        else
        {
            os << "rt_pow_f64_chkdom: overflow";
            RuntimeBridge::trap(TrapKind::Overflow, os.str(), loc, fn, block);
        }
        return true;
    }

    if (sig.retType.kind == Type::Kind::F64 && !std::isfinite(buffers.f64))
    {
        std::ostringstream os;
        os << "rt_pow_f64_chkdom: overflow";
        RuntimeBridge::trap(TrapKind::Overflow, os.str(), loc, fn, block);
        return true;
    }

    return false;
}

static void assignCallResult(Slot &destination,
                             const RuntimeSignature &signature,
                             const ResultBuffers &buffers)
{
    assignResult(destination, signature.retType.kind, buffers);
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

    PowStatus powStatus;
    auto rawArgs = marshalArguments(*desc, args, powStatus);

    ResultBuffers buffers;
    void *resultPtr = resultBufferFor(desc->signature.retType.kind, buffers);

    desc->handler(rawArgs.empty() ? nullptr : rawArgs.data(), resultPtr);

    if (handlePowTrap(*desc, powStatus, args, buffers, loc, fn, block))
        return res;

    assignCallResult(res, desc->signature, buffers);
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
            ctx.vm->currentContext.loc = loc;
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
