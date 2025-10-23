//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

#include "rt_string.h"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <sstream>
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
ViperString toViperString(StringRef text)
{
    if (text.empty())
    {
        if (text.data() == nullptr)
            return rt_const_cstr("");
        return rt_string_from_bytes(text.data(), 0);
    }
    if (text.data() == nullptr)
        return nullptr;
    if (text.find('\0') != StringRef::npos)
        return rt_string_from_bytes(text.data(), text.size());

    const char *data = text.data();
    size_t measuredLength = 0;
    bool haveMeasuredLength = false;
    if (data != nullptr)
    {
        measuredLength = std::char_traits<char>::length(data);
        haveMeasuredLength = true;
    }

    if (!haveMeasuredLength || measuredLength != text.size())
        return rt_string_from_bytes(data, text.size());

    return rt_const_cstr(data);
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
        RuntimeBridge::trap(TrapKind::DomainError,
                            "rt_string reported negative length",
                            {},
                            "",
                            "");
        return {};
    }
    return StringRef{data, static_cast<size_t>(length)};
}

/// @brief Convert a constant VM value into a 64-bit integer.
/// @details Handles integer, floating, and null pointer constants.  Other value
///          kinds are programmer errors and trigger an assertion so that new
///          kinds must update the marshalling layer explicitly.
/// @param value Constant VM value to convert.
/// @return 64-bit integer representation of @p value.
int64_t toI64(const il::core::Value &value)
{
    using Kind = il::core::Value::Kind;
    switch (value.kind)
    {
        case Kind::ConstInt:
            return static_cast<int64_t>(value.i64);
        case Kind::ConstFloat:
            return static_cast<int64_t>(value.f64);
        case Kind::NullPtr:
            return 0;
        default:
            assert(false && "value kind is not convertible to i64");
            return 0;
    }
}

/// @brief Convert a constant VM value into a 64-bit floating point number.
/// @details Mirrors @ref toI64 but produces a double precision result.  Integer
///          constants are cast, null pointers yield zero, and unsupported kinds
///          assert during development builds.
/// @param value Constant VM value to convert.
/// @return 64-bit floating point representation of @p value.
double toF64(const il::core::Value &value)
{
    using Kind = il::core::Value::Kind;
    switch (value.kind)
    {
        case Kind::ConstFloat:
            return value.f64;
        case Kind::ConstInt:
            return static_cast<double>(value.i64);
        case Kind::NullPtr:
            return 0.0;
        default:
            assert(false && "value kind is not convertible to f64");
            return 0.0;
    }
}

void *slotToArgPointer(Slot &slot, il::core::Type::Kind kind)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.slotAccessor)
    {
        std::ostringstream os;
        os << "runtime bridge does not support argument kind '" << il::core::kindToString(kind) << "'";
        RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), {}, "", "");
        return nullptr;
    }
    return entry.slotAccessor(slot);
}

void *resultBufferFor(il::core::Type::Kind kind, ResultBuffers &buffers)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.resultAccessor)
    {
        std::ostringstream os;
        os << "runtime bridge does not support return kind '" << il::core::kindToString(kind) << "'";
        RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), {}, "", "");
        return nullptr;
    }
    return entry.resultAccessor(buffers);
}

void assignResult(Slot &slot, il::core::Type::Kind kind, const ResultBuffers &buffers)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.assignResult)
    {
        std::ostringstream os;
        os << "runtime bridge cannot assign return kind '" << il::core::kindToString(kind) << "'";
        RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), {}, "", "");
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

    if (!powStatus.ok)
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

} // namespace il::vm
