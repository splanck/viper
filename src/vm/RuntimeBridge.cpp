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
            switch (kind)
            {
                case Type::Kind::I64:
                case Type::Kind::I1:
                    rawArgs[i] = static_cast<void *>(&slot.i64);
                    break;
                case Type::Kind::F64:
                    rawArgs[i] = static_cast<void *>(&slot.f64);
                    break;
                case Type::Kind::Ptr:
                    rawArgs[i] = static_cast<void *>(&slot.ptr);
                    break;
                case Type::Kind::Str:
                    rawArgs[i] = static_cast<void *>(&slot.str);
                    break;
                default:
                    assert(false && "unsupported runtime argument kind");
            }
        }

        void *resultPtr = nullptr;
        int64_t i64Result = 0;
        double f64Result = 0.0;
        rt_string strResult = nullptr;
        void *ptrResult = nullptr;

        switch (sig.retType.kind)
        {
            case Type::Kind::Void:
                break;
            case Type::Kind::I1:
            case Type::Kind::I64:
                resultPtr = &i64Result;
                break;
            case Type::Kind::F64:
                resultPtr = &f64Result;
                break;
            case Type::Kind::Str:
                resultPtr = &strResult;
                break;
            case Type::Kind::Ptr:
                resultPtr = &ptrResult;
                break;
            default:
                assert(false && "unsupported runtime return kind");
        }

        desc->handler(rawArgs.empty() ? nullptr : rawArgs.data(), resultPtr);

        switch (sig.retType.kind)
        {
            case Type::Kind::Void:
                break;
            case Type::Kind::I1:
            case Type::Kind::I64:
                res.i64 = i64Result;
                break;
            case Type::Kind::F64:
                res.f64 = f64Result;
                break;
            case Type::Kind::Str:
                res.str = strResult;
                break;
            case Type::Kind::Ptr:
                res.ptr = ptrResult;
                break;
            default:
                break;
        }
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
