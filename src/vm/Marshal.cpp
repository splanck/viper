//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/Marshal.cpp
// Purpose: Implement conversions between VM value wrappers and runtime bridge types.
// Invariants: Slot/string conversions must mirror the runtime C ABI exactly so
//             VM and native executions observe identical semantics, and helper
//             tables must be updated whenever @ref il::core::Type gains new
//             kinds.
// Ownership/Lifetime: Returned views borrow storage from runtime-managed strings
//                     and therefore remain valid only as long as the originating
//                     runtime object stays alive.
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
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

namespace il::vm
{

namespace
{
using il::core::Type;

/// @brief Bundle accessor callbacks for a specific IL type kind.
/// @details Each @ref KindAccessors record holds the function pointers needed to
///          expose VM @ref Slot storage to the runtime bridge.  Slot accessors
///          return pointers to mutable slot payloads, result accessors expose the
///          matching entry inside a @ref ResultBuffers aggregate, and assigners
///          move values back into slots after bridge calls complete.
struct KindAccessors
{
    using SlotAccessor = void *(*)(Slot &);
    using ResultAccessor = void *(*)(ResultBuffers &);
    using ResultAssigner = void (*)(Slot &, const ResultBuffers &);

    SlotAccessor slotAccessor = nullptr;
    ResultAccessor resultAccessor = nullptr;
    ResultAssigner assignResult = nullptr;
};

/// @brief Enumerate the IL kinds the runtime bridge is capable of marshalling.
/// @details The list doubles as an index set for @ref kKindAccessors so we can
///          precompute a dense lookup table keyed by @ref Type::Kind ordinals.
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

/// @brief Return a null pointer for result kinds that have no dedicated buffer.
/// @details Void, error, and resume-token results do not occupy storage in
///          @ref ResultBuffers.  The accessor communicates that by always
///          returning @c nullptr.
constexpr void *nullResultBuffer(ResultBuffers &)
{
    return nullptr;
}

/// @brief Ignore result assignment for kinds that carry no payload.
/// @details Error and resume-token kinds are represented entirely through VM
///          state, so the assigner simply performs no work while keeping the
///          table entries structurally uniform.
constexpr void assignNoop(Slot &, const ResultBuffers &)
{
}

/// @brief Produce a pointer to a member inside a @ref Slot instance.
/// @details The accessor template is instantiated for each supported slot field
///          (integer, floating-point, pointer, string) and returns a mutable
///          pointer suitable for passing to the runtime bridge.
template <auto Member> constexpr void *slotMemberAccessor(Slot &slot)
{
    return static_cast<void *>(&(slot.*Member));
}

/// @brief Produce a pointer to a member inside @ref ResultBuffers.
/// @details Mirrors @ref slotMemberAccessor but targets the structure that
///          receives return values from runtime calls.
template <auto Member> constexpr void *bufferMemberAccessor(ResultBuffers &buffers)
{
    return static_cast<void *>(&(buffers.*Member));
}

/// @brief Copy a value from @ref ResultBuffers back into a VM slot.
/// @details Instantiated per-kind to ensure the correct slot field is written
///          after a runtime call produces a result.
template <auto SlotMember, auto BufferMember>
constexpr void assignFromBuffer(Slot &slot, const ResultBuffers &buffers)
{
    slot.*SlotMember = buffers.*BufferMember;
}

/// @brief Construct accessor callbacks for kinds that transport no data.
/// @details Void, error, and resume-token kinds all share the same accessor
///          triple: no slot pointer, null result buffer, and no-op assignment.
constexpr KindAccessors makeVoidAccessors()
{
    return KindAccessors{nullptr, &nullResultBuffer, &assignNoop};
}

/// @brief Construct accessor callbacks for data-bearing kinds.
/// @details Binds the correct slot and buffer member functions together with an
///          assigner that copies the buffer value back into the VM slot.
template <auto SlotMember, auto BufferMember> constexpr KindAccessors makeAccessors()
{
    return KindAccessors{
        &slotMemberAccessor<SlotMember>,
        &bufferMemberAccessor<BufferMember>,
        &assignFromBuffer<SlotMember, BufferMember>,
    };
}

/// @brief Lookup table mapping IL kinds to accessor callbacks.
/// @details The table is constructed at startup using a constexpr lambda so the
///          runtime can perform O(1) dispatch without branching.  Unsupported
///          kinds retain the default-initialised null pointers which downstream
///          helpers detect and translate into traps.
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

/// @brief Retrieve the accessor bundle corresponding to an IL kind.
/// @details Performs a bounds check when assertions are enabled and returns the
///          precomputed entry for @p kind.  Callers inspect the result to decide
///          whether a particular type is marshalable.
/// @param kind IL type kind requested by a bridge operation.
/// @return Reference to the accessor triple for @p kind.
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

/// @brief Return a pointer to the payload stored in a VM slot.
/// @details Uses @ref dispatchFor to identify the accessor triple for @p kind
///          and reports a trap when the runtime bridge lacks support for the
///          requested type.  The returned pointer addresses storage inside
///          @p slot, allowing runtime helpers to operate directly on VM data.
/// @param slot Slot whose payload should be exposed to the runtime bridge.
/// @param kind IL type describing how the slot should be interpreted.
/// @return Mutable pointer to the slot payload, or @c nullptr when unsupported.
void *slotToArgPointer(Slot &slot, il::core::Type::Kind kind)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.slotAccessor)
    {
        std::ostringstream os;
        os << "runtime bridge does not support argument kind '" << il::core::kindToString(kind)
           << "'";
        RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), {}, "", "");
        return nullptr;
    }
    return entry.slotAccessor(slot);
}

/// @brief Produce a pointer to the result buffer for a given IL kind.
/// @details Runtime bridge calls populate the @ref ResultBuffers aggregate. This
///          helper selects the correct member based on @p kind and traps when no
///          storage exists for that type (for example, @c void results).
/// @param kind Return type requested by the runtime descriptor.
/// @param buffers Aggregate containing per-kind result storage.
/// @return Pointer to the buffer member associated with @p kind.
void *resultBufferFor(il::core::Type::Kind kind, ResultBuffers &buffers)
{
    const auto &entry = dispatchFor(kind);
    if (!entry.resultAccessor)
    {
        std::ostringstream os;
        os << "runtime bridge does not support return kind '" << il::core::kindToString(kind)
           << "'";
        RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), {}, "", "");
        return nullptr;
    }
    return entry.resultAccessor(buffers);
}

/// @brief Copy a runtime result back into a VM slot.
/// @details Looks up the assigner associated with @p kind and either transfers
///          the data or reports a trap when the runtime bridge lacks an
///          assignment strategy for that type.
/// @param slot Destination slot owned by the VM frame.
/// @param kind IL kind describing the returned value.
/// @param buffers Result aggregate filled by the runtime bridge.
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

/// @brief Translate VM call arguments into the runtime bridge ABI format.
/// @details Allocates a contiguous array of pointers sized to the runtime
///          signature, storing the address of each VM slot payload followed by
///          any hidden parameters required by the descriptor.  Power-intrinsic
///          calls trigger additional status wiring so domain errors can be
///          reported consistently.
/// @param sig Runtime signature describing parameter and hidden argument kinds.
/// @param args Slots containing the evaluated argument values.
/// @param powStatus Scratch status record used when power intrinsics require it.
/// @return Array of raw argument pointers ready for @c RuntimeBridge::invoke.
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

/// @brief Interpret the status of a runtime power intrinsic invocation.
/// @details Examines the descriptor and @p powStatus scratch space to determine
///          whether the helper trapped due to a domain or overflow error.  The
///          routine also checks the returned floating-point value for non-finite
///          results when the signature indicates a double return type.
/// @param desc Runtime descriptor that may classify power traps specially.
/// @param powStatus Scratch status populated during @ref marshalArguments.
/// @param args Argument slots originally passed to the runtime helper.
/// @param buffers Result storage filled by the runtime helper.
/// @return Outcome structure indicating whether a trap occurred and its kind.
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

/// @brief Construct a slot containing the runtime call result.
/// @details Delegates to @ref assignResult so that string and numeric ownership
///          rules mirror the rest of the marshalling layer.
/// @param signature Signature describing the return value kind.
/// @param buffers Result aggregate filled by the runtime helper.
/// @return Slot containing the return value, ready for placement into a frame.
Slot assignCallResult(const il::runtime::RuntimeSignature &signature, const ResultBuffers &buffers)
{
    Slot destination{};
    assignResult(destination, signature.retType.kind, buffers);
    return destination;
}

} // namespace il::vm
