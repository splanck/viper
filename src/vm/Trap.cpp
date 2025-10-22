//===----------------------------------------------------------------------===//
//
// This file is part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/vm/Trap.cpp
// Purpose: Provide helpers that materialise, inspect, and report VM trap states.
// Key invariants: Trap tokens are owned either by the active VM instance or the
//                 thread-local fallback and must be marked valid before use.
// Ownership/Lifetime: Functions borrow VM state; thread-local fallbacks persist
//                     only for the lifetime of the thread and avoid heap
//                     allocation.
// Links: docs/runtime-vm.md#traps
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

/// @brief Convert a trap kind enumerator to a human-readable mnemonic.
///
/// @details Provides canonical string forms for log messages and diagnostics.
///          Unknown enumerators degrade to "RuntimeError" so callers always
///          receive a stable token even when future kinds are introduced.
///
/// @param kind Trap kind to stringify.
/// @return Mnemonic describing @p kind.
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

/// @brief Map a raw runtime integer value to the corresponding trap kind.
///
/// @details Accepts the integer encoding emitted by the runtime and converts it
///          back into the strongly typed enumeration.  Unexpected values fall
///          back to @ref TrapKind::RuntimeError so defensive callers can treat
///          out-of-range inputs as generic failures.
///
/// @param value Integer representation of a trap kind.
/// @return Equivalent enumeration value.
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

/// @brief Acquire a mutable trap token for recording runtime errors.
///
/// @details When a VM instance is active the function returns a pointer to its
///          owned trap token after clearing any previous data.  Otherwise it
///          falls back to thread-local storage so callers outside the VM can
///          still materialise traps (for example, during unit tests).
///
/// @return Pointer to a writable trap token associated with the current thread.
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

/// @brief Retrieve the currently active trap token when one exists.
///
/// @details Consults the active VM first and validates that the token is marked
///          as valid before returning it.  If the VM is absent the thread-local
///          fallback is checked.  A null pointer indicates no trap is currently
///          armed.
///
/// @return Pointer to the active trap token or nullptr when none is available.
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

/// @brief Attach a human-readable message to the active trap token.
///
/// @details Updates the VM-owned token when present, otherwise records the
///          message in thread-local storage.  Marking the token as valid ensures
///          subsequent queries recognise that a trap has been produced.
///
/// @param text Message describing the trap for diagnostics.
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

/// @brief Fetch the message associated with the current trap token.
///
/// @details Returns the VM-owned message when a VM is active, otherwise the
///          thread-local fallback message.  Callers typically forward this text
///          to users or logs.
///
/// @return Copy of the currently recorded trap message.
std::string vm_current_trap_message()
{
    if (auto *vm = VM::activeInstance())
        return vm->trapToken.message;
    return tlsTrapMessage;
}

/// @brief Format a trap error and frame information into a printable string.
///
/// @details Consolidates function name, instruction pointer, and line
///          information into a concise diagnostic.  Missing data defaults to
///          placeholder values so the resulting string is still informative.
///
/// @param error Trap token describing the failure.
/// @param frame Frame metadata captured when the trap surfaced.
/// @return Human-readable description of the trap.
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

/// @brief Raise a trap using the supplied error description.
///
/// @details Normalises instruction pointer and line metadata against the active
///          VM context, allows the VM to intercept the trap via
///          @ref VM::prepareTrap, and records frame information for later
///          reporting.  When no handler is installed the runtime abort helper is
///          invoked to terminate execution with the formatted message.
///
/// @param input Trap information describing the failure.
void vm_raise_from_error(const VmError &input)
{
    VmError error = input;
    FrameInfo frame{};
    std::string message;

    if (auto *vm = VM::activeInstance())
    {
        if (error.ip == 0 && vm->currentContext.hasInstruction)
            error.ip = static_cast<uint64_t>(vm->currentContext.instructionIndex);
        if (error.line < 0 && vm->currentContext.loc.hasLine())
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

/// @brief Convenience wrapper that raises a trap from a kind/code pair.
///
/// @details Populates a @ref VmError structure with the provided metadata and
///          enriches it with instruction pointer and line information from the
///          active VM when available.  Control is then delegated to
///          @ref vm_raise_from_error for final processing.
///
/// @param kind Trap classification to raise.
/// @param code Optional runtime-specific error code.
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
        if (vm->currentContext.loc.hasLine())
            error.line = static_cast<int32_t>(vm->currentContext.loc.line);
    }

    vm_raise_from_error(error);
}

} // namespace il::vm
