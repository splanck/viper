// File: src/vm/RuntimeBridge.cpp
// Purpose: Bridges VM to C runtime functions.
// License: MIT License
// Key invariants: None.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-spec.md

#include "vm/RuntimeBridge.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "vm/VM.hpp"
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

/// @brief Translate a VM slot to the pointer expected by the runtime handler.
/// @param slot Slot containing the argument value.
/// @param kind IL type kind describing the slot contents.
/// @return Pointer to the slot member corresponding to @p kind.
void *slotToArgPointer(Slot &slot, Type::Kind kind)
{
    switch (kind)
    {
        case Type::Kind::I1:
        case Type::Kind::I64:
            return static_cast<void *>(&slot.i64);
        case Type::Kind::F64:
            return static_cast<void *>(&slot.f64);
        case Type::Kind::Ptr:
            return static_cast<void *>(&slot.ptr);
        case Type::Kind::Str:
            return static_cast<void *>(&slot.str);
        default:
            assert(false && "unsupported runtime argument kind");
            return nullptr;
    }
}

/// @brief Obtain the buffer address to receive a runtime result of @p kind.
/// @param kind Type kind of the runtime return value.
/// @param buffers Temporary storage for return values.
/// @return Pointer handed to the runtime handler for writing the result.
void *resultBufferFor(Type::Kind kind, ResultBuffers &buffers)
{
    switch (kind)
    {
        case Type::Kind::Void:
            return nullptr;
        case Type::Kind::I1:
        case Type::Kind::I64:
            return static_cast<void *>(&buffers.i64);
        case Type::Kind::F64:
            return static_cast<void *>(&buffers.f64);
        case Type::Kind::Str:
            return static_cast<void *>(&buffers.str);
        case Type::Kind::Ptr:
            return static_cast<void *>(&buffers.ptr);
        default:
            assert(false && "unsupported runtime return kind");
            return nullptr;
    }
}

/// @brief Store the marshalled runtime result back into VM slot @p slot.
/// @param slot Destination VM slot.
/// @param kind Type kind describing the expected slot member.
/// @param buffers Temporary storage containing the runtime result.
void assignResult(Slot &slot, Type::Kind kind, const ResultBuffers &buffers)
{
    switch (kind)
    {
        case Type::Kind::Void:
            break;
        case Type::Kind::I1:
        case Type::Kind::I64:
            slot.i64 = buffers.i64;
            break;
        case Type::Kind::F64:
            slot.f64 = buffers.f64;
            break;
        case Type::Kind::Str:
            slot.str = buffers.str;
            break;
        case Type::Kind::Ptr:
            slot.ptr = buffers.ptr;
            break;
        default:
            assert(false && "unsupported runtime return kind");
            break;
    }
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
        if (args.size() < count)
        {
            std::ostringstream os;
            os << name << ": expected " << count << " argument(s), got " << args.size();
            RuntimeBridge::trap(os.str(), loc, fn, block);
            return false;
        }
        return true;
    };
    const auto *desc = il::runtime::findRuntimeDescriptor(name);
    if (!desc)
    {
        assert(false && "unknown runtime call");
    }
    else if (checkArgs(desc->signature.paramTypes.size()))
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
