// File: src/vm/RuntimeBridge.cpp
// Purpose: Bridges VM to C runtime functions.
// License: MIT License
// Key invariants: None.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-spec.md

#include "vm/RuntimeBridge.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "vm/VM.hpp"
#include <array>
#include <cassert>
#include <sstream>

using il::support::SourceLoc;

namespace
{
using il::vm::RuntimeCallContext;
using il::vm::Slot;
using il::core::Type;

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

constexpr std::array<Type::Kind, 6> kSupportedKinds = {
    Type::Kind::Void,
    Type::Kind::I1,
    Type::Kind::I64,
    Type::Kind::F64,
    Type::Kind::Ptr,
    Type::Kind::Str,
};

static_assert(kSupportedKinds.size() == 6, "update kind accessors when Type::Kind grows");

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
    table[static_cast<size_t>(Type::Kind::I64)] = makeAccessors<&Slot::i64, &ResultBuffers::i64>();
    table[static_cast<size_t>(Type::Kind::F64)] = makeAccessors<&Slot::f64, &ResultBuffers::f64>();
    table[static_cast<size_t>(Type::Kind::Ptr)] = makeAccessors<&Slot::ptr, &ResultBuffers::ptr>();
    table[static_cast<size_t>(Type::Kind::Str)] = makeAccessors<&Slot::str, &ResultBuffers::str>();
    return table;
}();

static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::Void)].slotAccessor == nullptr,
              "Void must not expose a slot accessor");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::Void)].resultAccessor == &nullResultBuffer,
              "Void must map to the null result buffer");
static_assert(kKindAccessors[static_cast<size_t>(Type::Kind::I1)].slotAccessor ==
                  &slotMemberAccessor<&Slot::i64>,
              "I1 slot accessor must target Slot::i64");
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

void *reportUnsupportedPointer(const char *msg)
{
    assert(false && msg);
    return nullptr;
}

void reportUnsupportedAssign(const char *msg)
{
    assert(false && msg);
}

/// @brief Translate a VM slot to the pointer expected by the runtime handler.
/// @param slot Slot containing the argument value.
/// @param kind IL type kind describing the slot contents.
/// @return Pointer to the slot member corresponding to @p kind.
void *slotToArgPointer(Slot &slot, Type::Kind kind)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.slotAccessor)
        return reportUnsupportedPointer("unsupported runtime argument kind");
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
        return reportUnsupportedPointer("unsupported runtime return kind");
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
        reportUnsupportedAssign("unsupported runtime return kind");
        return;
    }
    entry.assignResult(slot, buffers);
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
        }
        tlsContext = previous;
    }
};

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
        il::vm::RuntimeBridge::trap(trapMsg, ctx->loc, ctx->function, ctx->block);
    else
        il::vm::RuntimeBridge::trap(trapMsg, {}, "", "");
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
    auto checkArgs = [&](size_t count)
    {
        if (args.size() != count)
        {
            std::ostringstream os;
            os << name << ": expected " << count << " argument(s), got " << args.size();
            if (args.size() > count)
                os << " (excess runtime operands)";
            RuntimeBridge::trap(os.str(), loc, fn, block);
            return false;
        }
        return true;
    };
    const auto *desc = il::runtime::findRuntimeDescriptor(name);
    if (!desc)
    {
        std::ostringstream os;
        os << "attempted to call unknown runtime helper '" << name << '\'';
        RuntimeBridge::trap(os.str(), loc, fn, block);
        return res;
    }
    if (checkArgs(desc->signature.paramTypes.size()))
    {
        const auto &sig = desc->signature;
        std::vector<void *> rawArgs(sig.paramTypes.size());
        for (size_t i = 0; i < sig.paramTypes.size(); ++i)
        {
            auto kind = sig.paramTypes[i].kind;
            Slot &slot = const_cast<Slot &>(args[i]);
            rawArgs[i] = slotToArgPointer(slot, kind);
        }

        ResultBuffers buffers;
        void *resultPtr = resultBufferFor(sig.retType.kind, buffers);

        desc->handler(rawArgs.empty() ? nullptr : rawArgs.data(), resultPtr);

        assignResult(res, sig.retType.kind, buffers);
    }
    return res;
}

/// @brief Report a trap originating from the C runtime.
/// @details Invoked by `vm_trap` when a runtime builtin signals a fatal
/// condition. Formats the message with optional function, block, and source
/// location before forwarding it to `rt_abort`.
/// @param msg   Description of the trap condition.
/// @param loc   Source location of the trapping instruction, if available.
/// @param fn    Fully qualified function name containing the call.
/// @param block Label of the basic block with the trapping call.
void RuntimeBridge::trap(const std::string &msg,
                         const SourceLoc &loc,
                         const std::string &fn,
                         const std::string &block)
{
    std::ostringstream os;
    os << msg;
    if (!fn.empty())
    {
        os << ' ' << fn << ": " << block;
        if (loc.isValid())
            os << " (" << loc.file_id << ':' << loc.line << ':' << loc.column << ')';
    }
    rt_abort(os.str().c_str());
}

const RuntimeCallContext *RuntimeBridge::activeContext()
{
    return tlsContext;
}

} // namespace il::vm
