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
using il::vm::Slot;
using il::core::Type;

/// @brief Global scratch space for recording the current source location.
/// @details Runtime calls may trigger traps inside the C runtime. The VM
/// populates these globals prior to dispatch so that the hook `vm_trap` can
/// report a precise function/block/SourceLoc. They are cleared after every call.
SourceLoc curLoc{};
/// @brief Fully qualified name of the function currently executing.
std::string curFn;
/// @brief Label of the current basic block within the function.
std::string curBlock;

} // namespace

/// @brief Entry point invoked from the C runtime when a trap occurs.
/// @details Serves as the external hook that the C runtime calls when
/// `rt_abort`-style routines detect a fatal condition. The VM stores call-site
/// context in globals via `RuntimeBridge::call`; this hook relays the trap
/// through `RuntimeBridge::trap` so diagnostics carry function, block, and
/// source information.
extern "C" void vm_trap(const char *msg)
{
    il::vm::RuntimeBridge::trap(msg ? msg : "trap", curLoc, curFn, curBlock);
}

namespace il::vm
{

/// @brief Dispatch a VM runtime call to the corresponding C implementation.
/// @details Establishes the trap bookkeeping for the duration of the call,
/// validates the arity against the lazily initialized dispatch table, and then
/// executes the bound C adapter. Any trap that fires while the callee runs is
/// able to surface precise context through the globals populated here.
Slot RuntimeBridge::call(const std::string &name,
                         const std::vector<Slot> &args,
                         const SourceLoc &loc,
                         const std::string &fn,
                         const std::string &block)
{
    // Stash call site info so `vm_trap` can find it if the callee traps.
    curLoc = loc;
    curFn = fn;
    curBlock = block;
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
    curLoc = {};
    curFn.clear();
    curBlock.clear();
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

} // namespace il::vm
