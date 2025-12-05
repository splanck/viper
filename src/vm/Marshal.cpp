//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/Marshal.cpp
// Purpose: Implement conversions between VM value wrappers and runtime bridge types.
// Ownership/Lifetime: Returned views borrow storage from runtime-managed strings.
// Links: docs/runtime-vm.md#marshalling
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides helpers for converting values between VM and runtime layers.
/// @details Collects the string and scalar conversion routines used by opcode
///          handlers so that ownership semantics and error handling remain
///          consistent across the VM.

#include "vm/Marshal.hpp"

#include "viper/runtime/rt.h"
#include "vm/DiagFormat.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace il::vm
{

namespace
{
using il::core::Type;

struct KindAccessors
{
    using SlotAccessor = void *(*)(Slot &);
    using ResultAccessor = void *(*)(ResultBuffers &);
    using ResultAssigner = void (*)(Slot &, const ResultBuffers &);

    SlotAccessor slotAccessor = nullptr;
    ResultAccessor resultAccessor = nullptr;
    ResultAssigner assignResult = nullptr;
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

constexpr void assignNoop(Slot &, const ResultBuffers &) {}

template <auto Member> constexpr void *slotMemberAccessor(Slot &slot)
{
    return static_cast<void *>(&(slot.*Member));
}

template <auto Member> constexpr void *bufferMemberAccessor(ResultBuffers &buffers)
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

template <auto SlotMember, auto BufferMember> constexpr KindAccessors makeAccessors()
{
    return KindAccessors{
        &slotMemberAccessor<SlotMember>,
        &bufferMemberAccessor<BufferMember>,
        &assignFromBuffer<SlotMember, BufferMember>,
    };
}

constexpr std::array<KindAccessors, kSupportedKinds.size()> kKindAccessors = []
{
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

const KindAccessors &dispatchFor(Type::Kind kind)
{
    const auto index = static_cast<size_t>(kind);
    assert(index < kKindAccessors.size() && "invalid type kind");
    return kKindAccessors[index];
}

} // namespace

/// @brief Convert an immutable VM string view into a runtime handle.
/// @details Preserves the `nullptr` sentinel used throughout the VM to mean "no
///          string" and reuses the runtime's constant-string fast path when the
///          input has no embedded NULs.  Otherwise a fresh runtime allocation
///          mirrors the byte sequence so handlers can safely share the returned
///          handle.
/// @param text Non-owning reference to the source character range.
/// @return Runtime handle suitable for passing to C helpers; may be null when
///         @p text lacks backing storage.
ViperString toViperString(StringRef text, AssumeNullTerminated assumeNullTerminated)
{
    if (text.data() == nullptr)
        return nullptr;
    if (text.empty())
        return rt_string_from_bytes(text.data(), 0);
    if (text.find('\0') != StringRef::npos)
        return rt_string_from_bytes(text.data(), text.size());

    if (assumeNullTerminated == AssumeNullTerminated::Yes)
        return rt_const_cstr(text.data());

    return rt_string_from_bytes(text.data(), text.size());
}

/// @brief Convert a runtime string handle back into the VM's view type.
/// @details Valid runtime handles expose a contiguous UTF-8 byte sequence and
///          length via the C ABI helpers.  The returned @ref StringRef borrows
///          that storage without taking ownership, so callers must ensure the
///          runtime string outlives the view.  Null or invalid handles produce
///          an empty view and, in the negative-length case, raise a runtime trap.
/// @param str Runtime string handle to translate.
/// @return Non-owning view of the runtime string's contents, or an empty view
///         when the handle is null.
StringRef fromViperString(const ViperString &str)
{
    if (!str)
        return {};
    const char *data = rt_string_cstr(str);
    if (!data)
        return {};
    const int64_t length = rt_len(str);
    if (length < 0)
    {
        RuntimeBridge::trap(
            TrapKind::DomainError, "rt_string reported negative length", {}, "", "");
        return {};
    }
    if (!detail::lengthWithinLimit(length, kMaxBridgeStringBytes))
    {
        if (RuntimeBridge::hasActiveVm())
        {
            RuntimeBridge::trap(
                TrapKind::DomainError, "rt_string length exceeds bridge limit", {}, "", "");
        }
        return {};
    }
    if (!detail::lengthWithinLimit(length, std::numeric_limits<size_t>::max()))
        return {};
    return StringRef{data, static_cast<size_t>(length)};
}

//===----------------------------------------------------------------------===//
// Constant Scalar Conversion Helpers
//===----------------------------------------------------------------------===//
//
// These functions are intentionally restricted to constant values (ConstInt,
// ConstFloat, NullPtr). They are NOT general-purpose coercions.
//
// The precondition isConstantScalar(value) is checked via assertion. Callers
// should verify the value kind before calling if the kind is not statically
// known. This keeps the hot path cheap (no branch on success) while catching
// programmer errors during development.
//
//===----------------------------------------------------------------------===//

namespace
{
/// @brief Convert Value::Kind to a diagnostic string.
/// @param kind The value kind to describe.
/// @return Human-readable name for error messages.
constexpr const char *valueKindToString(il::core::Value::Kind kind) noexcept
{
    using Kind = il::core::Value::Kind;
    switch (kind)
    {
        case Kind::Temp:
            return "Temp";
        case Kind::ConstInt:
            return "ConstInt";
        case Kind::ConstFloat:
            return "ConstFloat";
        case Kind::ConstStr:
            return "ConstStr";
        case Kind::GlobalAddr:
            return "GlobalAddr";
        case Kind::NullPtr:
            return "NullPtr";
    }
    return "Unknown";
}
} // namespace

int64_t toI64(const il::core::Value &value)
{
    using Kind = il::core::Value::Kind;

    // Precondition: value must be a constant scalar.
    // This assertion catches programmer errors where toI64 is called on
    // non-constant values like Temp, ConstStr, or GlobalAddr.
    assert(isConstantScalar(value) &&
           "toI64 requires a constant scalar value (ConstInt, ConstFloat, or NullPtr)");

    switch (value.kind)
    {
        case Kind::ConstInt:
            return static_cast<int64_t>(value.i64);
        case Kind::ConstFloat:
            return static_cast<int64_t>(value.f64);
        case Kind::NullPtr:
            return 0;
        default:
            // In release builds without assertions, provide a clear diagnostic
            // before aborting. This should never be reached if assertions are
            // enabled, but guards against NDEBUG builds hiding bugs.
            std::fprintf(stderr,
                         "[FATAL] toI64 called with non-constant value kind: %s\n",
                         valueKindToString(value.kind));
            std::abort();
    }
}

double toF64(const il::core::Value &value)
{
    using Kind = il::core::Value::Kind;

    // Precondition: value must be a constant scalar.
    // This assertion catches programmer errors where toF64 is called on
    // non-constant values like Temp, ConstStr, or GlobalAddr.
    assert(isConstantScalar(value) &&
           "toF64 requires a constant scalar value (ConstInt, ConstFloat, or NullPtr)");

    switch (value.kind)
    {
        case Kind::ConstFloat:
            return value.f64;
        case Kind::ConstInt:
            return static_cast<double>(value.i64);
        case Kind::NullPtr:
            return 0.0;
        default:
            // In release builds without assertions, provide a clear diagnostic
            // before aborting. This should never be reached if assertions are
            // enabled, but guards against NDEBUG builds hiding bugs.
            std::fprintf(stderr,
                         "[FATAL] toF64 called with non-constant value kind: %s\n",
                         valueKindToString(value.kind));
            std::abort();
    }
}

void *slotToArgPointer(Slot &slot, il::core::Type::Kind kind)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.slotAccessor)
    {
        RuntimeBridge::trap(
            TrapKind::InvalidOperation, diag::formatUnsupportedKind("argument", kind), {}, "", "");
        return nullptr;
    }
    return entry.slotAccessor(slot);
}

void *resultBufferFor(il::core::Type::Kind kind, ResultBuffers &buffers)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.resultAccessor)
    {
        RuntimeBridge::trap(
            TrapKind::InvalidOperation, diag::formatUnsupportedKind("return", kind), {}, "", "");
        return nullptr;
    }
    return entry.resultAccessor(buffers);
}

void assignResult(Slot &slot, il::core::Type::Kind kind, const ResultBuffers &buffers)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.assignResult)
    {
        RuntimeBridge::trap(TrapKind::InvalidOperation,
                            diag::formatUnsupportedKind("assign return", kind),
                            {},
                            "",
                            "");
        return;
    }
    entry.assignResult(slot, buffers);
}

std::vector<void *> marshalArguments(const il::runtime::RuntimeSignature &sig,
                                     std::span<Slot> args,
                                     PowStatus &powStatus)
{
    std::vector<void *> rawArgs(sig.paramTypes.size() + sig.hiddenParams.size());

    for (size_t i = 0; i < sig.paramTypes.size(); ++i)
    {
        auto kind = sig.paramTypes[i].kind;
        Slot &slot = args[i];
        rawArgs[i] = slotToArgPointer(slot, kind);
    }

    size_t hiddenIndex = sig.paramTypes.size();
    for (const auto &hidden : sig.hiddenParams)
    {
        switch (hidden.kind)
        {
            case il::runtime::RuntimeHiddenParamKind::None:
                rawArgs[hiddenIndex++] = nullptr;
                break;
            case il::runtime::RuntimeHiddenParamKind::PowStatusPointer:
                powStatus.active = true;
                powStatus.ok = true;
                powStatus.ptr = &powStatus.ok;
                // Pow helpers expect a pointer to the status pointer so they can swap
                // it for a runtime-managed location when traps must propagate.
                rawArgs[hiddenIndex++] = &powStatus.ptr;
                break;
        }
    }

    return rawArgs;
}

PowTrapOutcome classifyPowTrap(const il::runtime::RuntimeDescriptor &desc,
                               const PowStatus &powStatus,
                               std::span<const Slot> args,
                               const ResultBuffers &buffers)
{
    PowTrapOutcome outcome{};
    if (desc.trapClass != il::runtime::RuntimeTrapClass::PowDomainOverflow || !powStatus.active)
        return outcome;

    bool okStatus = powStatus.ok;
    if (powStatus.ptr)
    {
        if (powStatus.ptr == &powStatus.ok)
        {
            okStatus = powStatus.ok;
        }
        else
        {
            okStatus = *powStatus.ptr;
        }
    }

    if (!okStatus)
    {
        const double base = !args.empty() ? args[0].f64 : 0.0;
        const double exp = (args.size() > 1) ? args[1].f64 : 0.0;
        const bool expIntegral = std::isfinite(exp) && (exp == std::trunc(exp));
        const bool domainError = (base < 0.0) && !expIntegral;

        outcome.triggered = true;
        if (domainError)
        {
            outcome.kind = TrapKind::DomainError;
            outcome.message = "rt_pow_f64_chkdom: negative base with fractional exponent";
        }
        else
        {
            outcome.kind = TrapKind::Overflow;
            outcome.message = "rt_pow_f64_chkdom: overflow";
        }
        return outcome;
    }

    if (desc.signature.retType.kind == il::core::Type::Kind::F64 && !std::isfinite(buffers.f64))
    {
        outcome.triggered = true;
        outcome.kind = TrapKind::Overflow;
        outcome.message = "rt_pow_f64_chkdom: overflow";
    }

    return outcome;
}

Slot assignCallResult(const il::runtime::RuntimeSignature &signature, const ResultBuffers &buffers)
{
    Slot destination{};
    assignResult(destination, signature.retType.kind, buffers);
    return destination;
}

//===----------------------------------------------------------------------===//
// Marshalling Validation Helpers
//===----------------------------------------------------------------------===//

MarshalValidation validateMarshalArity(const il::runtime::RuntimeDescriptor &desc,
                                       std::size_t argCount)
{
    return validateMarshalArity(desc.signature, argCount, desc.name);
}

MarshalValidation validateMarshalArity(const il::runtime::RuntimeSignature &sig,
                                       std::size_t argCount,
                                       std::string_view calleeName)
{
    MarshalValidation result;
    const auto expected = sig.paramTypes.size();
    if (argCount != expected)
    {
        result.ok = false;
        result.errorMessage.reserve(calleeName.size() + 64);
        result.errorMessage.append(calleeName);
        result.errorMessage.append(": expected ");
        result.errorMessage.append(std::to_string(expected));
        result.errorMessage.append(" argument(s), got ");
        result.errorMessage.append(std::to_string(argCount));
        if (argCount > expected)
            result.errorMessage.append(" (excess runtime operands)");
    }
    return result;
}

MarshalValidation validateMarshalArgs(const il::runtime::RuntimeDescriptor &desc,
                                      std::span<const Slot> args,
                                      bool checkNullPointers)
{
    // First check arity
    MarshalValidation result = validateMarshalArity(desc, args.size());
    if (!result.ok)
        return result;

    // Optionally validate pointer arguments
    if (checkNullPointers)
    {
        const auto &sig = desc.signature;
        for (std::size_t i = 0; i < sig.paramTypes.size() && i < args.size(); ++i)
        {
            if (sig.paramTypes[i].kind == il::core::Type::Kind::Ptr)
            {
                if (args[i].ptr == nullptr)
                {
                    result.ok = false;
                    result.errorMessage.reserve(desc.name.size() + 48);
                    result.errorMessage.append(desc.name);
                    result.errorMessage.append(": null pointer argument at index ");
                    result.errorMessage.append(std::to_string(i));
                    return result;
                }
            }
        }
    }

    return result;
}

std::vector<void *> marshalArgumentsValidated(const il::runtime::RuntimeDescriptor &desc,
                                              std::span<Slot> args,
                                              PowStatus &powStatus,
                                              MarshalValidation &validation,
                                              bool checkNullPointers)
{
    // Validate before marshalling to avoid out-of-bounds access
    validation = validateMarshalArgs(
        desc, std::span<const Slot>{args.data(), args.size()}, checkNullPointers);
    if (!validation.ok)
        return {};

    // Delegate to existing marshalling logic
    return marshalArguments(desc.signature, args, powStatus);
}

} // namespace il::vm
