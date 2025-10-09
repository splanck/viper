//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the VM-side trap helpers responsible for constructing runtime error
// messages, bridging traps raised outside an active VM instance, and routing
// the resulting diagnostics either back into the VM or to the standalone
// runtime bridge.  The helpers centralise the formatting rules and thread-local
// fallbacks so that trap handling remains deterministic across interpreter and
// host-driven entry points.
//
//===----------------------------------------------------------------------===//

#include "vm/Trap.hpp"

#include "vm/VM.hpp"
#include "rt.hpp"

#include <sstream>
#include <string_view>

#ifdef EOF
#pragma push_macro("EOF")
#undef EOF
#define IL_VM_TRAP_CPP_RESTORE_EOF 1
#endif

namespace il::vm
{

/// @brief Convert a trap kind enumerator into a canonical string.
///
/// The mapping is intentionally exhaustive to guarantee that user-visible trap
/// messages remain stable even when new trap kinds are added.  An unknown value
/// degrades gracefully by reporting `RuntimeError` so the caller still receives
/// a descriptive label.
///
/// @param kind Enumerated trap category.
/// @return Constant string describing @p kind.
std::string_view toString(TrapKind kind)
{
    switch (kind)
    {
        case TrapKind::DivideByZero:
            return "DivideByZero";
        case TrapKind::Overflow:
            return "Overflow";
        case TrapKind::InvalidCast:
            return "InvalidCast";
        case TrapKind::DomainError:
            return "DomainError";
        case TrapKind::Bounds:
            return "Bounds";
        case TrapKind::FileNotFound:
            return "FileNotFound";
        case TrapKind::EOF:
            return "EOF";
        case TrapKind::IOError:
            return "IOError";
        case TrapKind::InvalidOperation:
            return "InvalidOperation";
        case TrapKind::RuntimeError:
            return "RuntimeError";
    }

    // NOTE: Maintain graceful degradation when encountering unexpected enumerators.
    return "RuntimeError";
}

/// @brief Translate an integer payload into a trap kind.
///
/// IL bytecode and runtime bridges encode trap kinds as 32-bit integers.  The
/// helper validates the payload by comparing against every enumerator and
/// defaults to @ref TrapKind::RuntimeError when the value falls outside the
/// supported range.
///
/// @param value Integer provided by the trap source.
/// @return Matching @ref TrapKind when recognised; otherwise RuntimeError.
TrapKind trapKindFromValue(int32_t value)
{
    switch (value)
    {
        case static_cast<int32_t>(TrapKind::DivideByZero):
            return TrapKind::DivideByZero;
        case static_cast<int32_t>(TrapKind::Overflow):
            return TrapKind::Overflow;
        case static_cast<int32_t>(TrapKind::InvalidCast):
            return TrapKind::InvalidCast;
        case static_cast<int32_t>(TrapKind::DomainError):
            return TrapKind::DomainError;
        case static_cast<int32_t>(TrapKind::Bounds):
            return TrapKind::Bounds;
        case static_cast<int32_t>(TrapKind::FileNotFound):
            return TrapKind::FileNotFound;
        case static_cast<int32_t>(TrapKind::EOF):
            return TrapKind::EOF;
        case static_cast<int32_t>(TrapKind::IOError):
            return TrapKind::IOError;
        case static_cast<int32_t>(TrapKind::InvalidOperation):
            return TrapKind::InvalidOperation;
        case static_cast<int32_t>(TrapKind::RuntimeError):
            return TrapKind::RuntimeError;
        default:
            break;
    }

    // NOTE: Legacy IL payloads may encode unexpected values; fall back to RuntimeError.
    return TrapKind::RuntimeError;
}

#ifdef IL_VM_TRAP_CPP_RESTORE_EOF
#pragma pop_macro("EOF")
#undef IL_VM_TRAP_CPP_RESTORE_EOF
#endif

namespace
{
thread_local VmError tlsTrapError{};
thread_local std::string tlsTrapMessage;
thread_local bool tlsTrapValid = false;
} // namespace

/// @brief Acquire a writable trap token for the active execution context.
///
/// When a VM is executing, the token stored on the active instance is reused to
/// avoid additional allocations.  If no VM is active (for example when the C
/// runtime raises a trap before the interpreter starts) a thread-local fallback
/// token is provisioned.  In both cases the token is cleared before being
/// returned so callers can populate it without first resetting fields.
///
/// @return Pointer to a cleared trap token owned by either the VM or TLS.
VmError *vm_acquire_trap_token()
{
    if (auto *vm = VM::activeInstance())
    {
        vm->trapToken.error = {};
        vm->trapToken.message.clear();
        vm->trapToken.valid = true;
        return &vm->trapToken.error;
    }

    tlsTrapError = {};
    tlsTrapMessage.clear();
    tlsTrapValid = true;
    return &tlsTrapError;
}

/// @brief Inspect the most recently acquired trap token.
///
/// The function first checks the active VM for a valid token and falls back to
/// the thread-local storage used by @ref vm_acquire_trap_token when no VM is in
/// flight.  A null pointer indicates that no trap token has been provisioned on
/// the current thread.
///
/// @return Pointer to the active trap token, or nullptr when none exists.
const VmError *vm_current_trap_token()
{
    if (auto *vm = VM::activeInstance())
    {
        if (!vm->trapToken.valid)
            return nullptr;
        return &vm->trapToken.error;
    }
    if (!tlsTrapValid)
        return nullptr;
    return &tlsTrapError;
}

/// @brief Record the human-readable message associated with the trap token.
///
/// Messages are stored alongside the trap token and survive until either the
/// VM consumes them or the TLS fallback is cleared.  This ensures that trap
/// consumers—such as the debugger or host application—receive the final formatted
/// message even when the trap originated in C code.
///
/// @param text Message text to persist for later retrieval.
void vm_store_trap_token_message(std::string_view text)
{
    if (auto *vm = VM::activeInstance())
    {
        vm->trapToken.message.assign(text.begin(), text.end());
        vm->trapToken.valid = true;
        return;
    }

    tlsTrapMessage.assign(text.begin(), text.end());
    tlsTrapValid = true;
}

/// @brief Retrieve the current trap message for the active context.
///
/// Mirrors @ref vm_current_trap_token by checking the VM-owned trap store first
/// and then consulting the TLS fallback.  The returned string is a copy so
/// callers can retain it without worrying about the underlying storage.
///
/// @return Trap message accumulated by the most recent trap.
std::string vm_current_trap_message()
{
    if (auto *vm = VM::activeInstance())
        return vm->trapToken.message;
    return tlsTrapMessage;
}

/// @brief Format a VM error and execution frame into a diagnostic string.
///
/// Combines the trap metadata with the frame's function name, instruction
/// pointer, and source line to produce the canonical trap banner.  Default
/// placeholders are inserted when specific values are unavailable so that the
/// resulting string always contains meaningful context.
///
/// @param error Trap information describing the failure.
/// @param frame Execution frame captured at the point of failure.
/// @return Human-readable string describing @p error within @p frame.
std::string vm_format_error(const VmError &error, const FrameInfo &frame)
{
    const std::string &function = frame.function.empty() ? std::string("<unknown>") : frame.function;
    const uint64_t ip = error.ip ? error.ip : frame.ip;
    const int32_t line = error.line >= 0 ? error.line : (frame.line >= 0 ? frame.line : -1);

    std::ostringstream os;
    os << "Trap @" << function << '#' << ip << " line " << line << ": " << toString(error.kind)
       << " (code=" << error.code << ')';
    return os.str();
}

/// @brief Raise a trap using a fully populated error structure.
///
/// The helper normalises the provided error by filling in missing instruction
/// indices and line numbers from the active VM context when possible.  When the
/// VM installs a trap handler, the handler receives the recorded frame and
/// message; otherwise the runtime abort routine terminates the process with the
/// formatted error.
///
/// @param input Trap details to propagate to the active handler or runtime.
void vm_raise_from_error(const VmError &input)
{
    VmError error = input;
    FrameInfo frame{};
    std::string message;

    if (auto *vm = VM::activeInstance())
    {
        if (error.ip == 0 && vm->currentContext.hasInstruction)
            error.ip = static_cast<uint64_t>(vm->currentContext.instructionIndex);
        if (error.line < 0 && vm->currentContext.loc.isValid())
            error.line = static_cast<int32_t>(vm->currentContext.loc.line);

        if (vm->prepareTrap(error))
            return;

        frame = vm->buildFrameInfo(error);
        message = vm->recordTrap(error, frame);
    }
    else
    {
        frame.function = "<unknown>";
        frame.ip = error.ip;
        frame.line = error.line;
        frame.handlerInstalled = false;
        message = vm_format_error(error, frame);
    }

    if (!frame.handlerInstalled)
        rt_abort(message.c_str());
}

/// @brief Convenience wrapper that raises a trap from a kind and auxiliary code.
///
/// Populates a @ref VmError with the supplied kind and code, then forwards to
/// @ref vm_raise_from_error.  Instruction and source information are filled in
/// from the active VM context when available so that the resulting diagnostic is
/// precise even when the caller only supplies the trap category.
///
/// @param kind Enumerated trap category that triggered the failure.
/// @param code Implementation-defined numeric payload (for example errno).
void vm_raise(TrapKind kind, int32_t code)
{
    VmError error{};
    error.kind = kind;
    error.code = code;
    error.ip = 0;
    error.line = -1;

    if (auto *vm = VM::activeInstance())
    {
        if (vm->currentContext.hasInstruction)
            error.ip = static_cast<uint64_t>(vm->currentContext.instructionIndex);
        if (vm->currentContext.loc.isValid())
            error.line = static_cast<int32_t>(vm->currentContext.loc.line);
    }

    vm_raise_from_error(error);
}

} // namespace il::vm
