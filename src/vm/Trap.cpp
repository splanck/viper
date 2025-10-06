// File: src/vm/Trap.cpp
// Purpose: Implements VM trap helpers for structured error reporting.
// Key invariants: Trap formatting uses stable message templates.
// Ownership/Lifetime: Operates on the active VM instance only.
// Links: docs/specs/errors.md

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

std::string vm_current_trap_message()
{
    if (auto *vm = VM::activeInstance())
        return vm->trapToken.message;
    return tlsTrapMessage;
}

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
