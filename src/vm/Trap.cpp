//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the runtime trap helpers that translate between VM error objects,
// trap kinds, and user-facing error messages.  The functions in this translation
// unit mediate between the interpreter loop and the runtime bridge so both VM
// and native code paths produce consistent diagnostics.
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
std::string vm_current_trap_message()
{
    if (auto *vm = VM::activeInstance())
        return vm->trapToken.message;
    return tlsTrapMessage;
}

/// @brief Format a trap error and frame information into a printable string.
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

/// @brief Convenience wrapper that raises a trap from a kind/code pair.
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
