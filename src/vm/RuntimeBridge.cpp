// File: src/vm/RuntimeBridge.cpp
// Purpose: Bridges VM to C runtime functions.
// License: MIT License
// Key invariants: None.
// Ownership/Lifetime: Bridge does not own VM or runtime resources.
// Links: docs/il-guide.md#reference

#include "vm/RuntimeBridge.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "vm/SlotAccess.hpp"
#include "vm/VM.hpp"
#include "rt_fp.h"
#include <cassert>
#include <cmath>
#include <sstream>

using il::support::SourceLoc;

namespace
{
using il::vm::RuntimeCallContext;
using il::vm::Slot;
using il::vm::TrapKind;
using il::core::Type;
using il::vm::slot_access::ResultBuffers;

/// @brief Thread-local pointer to the runtime call context for active trap reporting.
thread_local RuntimeCallContext *tlsContext = nullptr;

void *reportUnsupportedPointer(const char *msg)
{
    assert(false && msg);
    return nullptr;
}

/// @brief Translate a VM slot to the pointer expected by the runtime handler.
/// @param slot Slot containing the argument value.
/// @param kind IL type kind describing the slot contents.
/// @return Pointer to the slot member corresponding to @p kind.
void *slotToArgPointer(Slot &slot, Type::Kind kind)
{
    void *ptr = il::vm::slot_access::slotPointer(slot, kind);
    if (!ptr)
        return reportUnsupportedPointer("unsupported runtime argument kind");
    return ptr;
}

/// @brief Obtain the buffer address to receive a runtime result of @p kind.
/// @param kind Type kind of the runtime return value.
/// @param buffers Temporary storage for return values.
/// @return Pointer handed to the runtime handler for writing the result.
void *resultBufferFor(Type::Kind kind, ResultBuffers &buffers)
{
    void *ptr = il::vm::slot_access::resultBuffer(kind, buffers);
    if (!ptr && kind != Type::Kind::Void && kind != Type::Kind::Error && kind != Type::Kind::ResumeTok)
        return reportUnsupportedPointer("unsupported runtime return kind");
    return ptr;
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
    auto checkArgs = [&](size_t count)
    {
        if (args.size() != count)
        {
            std::ostringstream os;
            os << name << ": expected " << count << " argument(s), got " << args.size();
            if (args.size() > count)
                os << " (excess runtime operands)";
            RuntimeBridge::trap(TrapKind::DomainError, os.str(), loc, fn, block);
            return false;
        }
        return true;
    };
    const auto *desc = il::runtime::findRuntimeDescriptor(name);
    if (!desc)
    {
        std::ostringstream os;
        os << "attempted to call unknown runtime helper '" << name << '\'';
        RuntimeBridge::trap(TrapKind::DomainError, os.str(), loc, fn, block);
        return res;
    }
    if (name == "rt_pow_f64_chkdom")
    {
        if (!checkArgs(2))
            return res;

        const double base = args[0].f64;
        const double exp = args[1].f64;
        const bool expIntegral = std::isfinite(exp) && (exp == std::trunc(exp));
        const bool domainError = (base < 0.0) && !expIntegral;

        bool ok = true;
        const double value = rt_pow_f64_chkdom(base, exp, &ok);
        if (!ok)
        {
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
            return res;
        }

        if (!std::isfinite(value))
        {
            std::ostringstream os;
            os << "rt_pow_f64_chkdom: overflow";
            RuntimeBridge::trap(TrapKind::Overflow, os.str(), loc, fn, block);
            return res;
        }

        res.f64 = value;
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

        il::vm::slot_access::assignResult(res, sig.retType.kind, buffers);
    }
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
    (void)msg;
    (void)block;
    if (auto *vm = VM::activeInstance())
    {
        if (loc.isValid())
            vm->currentContext.loc = loc;
        vm_raise(kind);
        return;
    }

    VmError error{};
    error.kind = kind;
    error.code = 0;
    error.ip = 0;
    error.line = loc.isValid() ? static_cast<int32_t>(loc.line) : -1;

    FrameInfo frame{};
    frame.function = fn.empty() ? std::string("<unknown>") : fn;
    frame.ip = 0;
    frame.line = error.line;
    frame.handlerInstalled = false;

    const std::string diagnostic = vm_format_error(error, frame);
    rt_abort(diagnostic.c_str());
}

const RuntimeCallContext *RuntimeBridge::activeContext()
{
    return tlsContext;
}

} // namespace il::vm
