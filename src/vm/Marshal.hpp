//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/Marshal.hpp
// Purpose: Declares helpers for converting between VM and runtime data types.
// Key invariants: Conversion helpers preserve existing runtime encodings.
// Ownership/Lifetime: Views returned do not extend the lifetime of underlying data.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Value.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "viper/runtime/rt.h"
#include "vm/Trap.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace il::vm
{

using StringRef = std::string_view;
using ViperString = ::rt_string;

/// @brief Indicates whether a string view is guaranteed to be null-terminated.
enum class AssumeNullTerminated : bool
{
    No = false,
    Yes = true,
};

union Slot;

namespace detail
{
/// @brief Check whether a runtime-provided string length fits within @p limit.
/// @param length Length reported by the runtime as a signed 64-bit value.
/// @param limit Maximum representable size for the destination view type.
/// @return True when @p length is non-negative and does not exceed @p limit.
/// @note Constexpr for compile-time validation when possible.
[[nodiscard]] constexpr bool lengthWithinLimit(int64_t length, uint64_t limit) noexcept
{
    return length >= 0 && static_cast<uint64_t>(length) <= limit;
}
} // namespace detail

/// @brief Maximum number of bytes the VM is willing to expose from a runtime string.
/// @details Strings larger than this limit are treated as invalid to avoid allocating
///          unbounded host buffers when marshalling corrupted runtime handles.
inline constexpr uint64_t kMaxBridgeStringBytes =
    static_cast<uint64_t>(std::numeric_limits<int32_t>::max());

struct ResultBuffers
{
    int64_t i64 = 0;
    double f64 = 0.0;
    ViperString str = nullptr;
    void *ptr = nullptr;
};

struct PowStatus
{
    bool active{false};
    bool ok{true};
    bool *ptr{nullptr};
};

struct PowTrapOutcome
{
    bool triggered{false};
    TrapKind kind{TrapKind::RuntimeError};
    std::string message;
};

ViperString toViperString(StringRef text,
                          AssumeNullTerminated assumeNullTerminated = AssumeNullTerminated::No);
StringRef fromViperString(const ViperString &str);

//===----------------------------------------------------------------------===//
// Constant Scalar Conversion Helpers
//===----------------------------------------------------------------------===//
//
// The following functions convert IL constant values to C++ scalar types.
// They are intentionally restricted to "constant scalar" kinds:
//   - ConstInt:   integer literal
//   - ConstFloat: floating-point literal
//   - NullPtr:    null pointer (treated as zero)
//
// These are NOT general-purpose coercions. Using them on Temp, ConstStr, or
// GlobalAddr values is a programmer error and will abort in debug builds.
// This restriction exists to catch misuse early and ensure new Value::Kind
// variants are handled explicitly rather than silently converting.
//
//===----------------------------------------------------------------------===//

/// @brief Check if a Value::Kind represents a constant that can be converted
///        to a scalar numeric type (i64 or f64).
/// @param kind The value kind to check.
/// @return True for ConstInt, ConstFloat, and NullPtr; false otherwise.
/// @note Constexpr for use in static assertions and compile-time checks.
[[nodiscard]] constexpr bool isConstantScalar(il::core::Value::Kind kind) noexcept
{
    using Kind = il::core::Value::Kind;
    return kind == Kind::ConstInt || kind == Kind::ConstFloat || kind == Kind::NullPtr;
}

/// @brief Check if a Value represents a constant that can be converted to a scalar.
/// @param value The value to check.
/// @return True if the value's kind is a constant scalar.
[[nodiscard]] inline bool isConstantScalar(const il::core::Value &value) noexcept
{
    return isConstantScalar(value.kind);
}

/// @brief Convert a constant VM value into a 64-bit integer.
/// @details Handles integer, floating, and null pointer constants. Other value
///          kinds are programmer errors and trigger an assertion so that new
///          kinds must update the marshalling layer explicitly.
/// @pre isConstantScalar(value) must be true.
/// @param value Constant VM value to convert.
/// @return 64-bit integer representation of @p value.
/// @note This is NOT a general-purpose coercion. It is intended only for
///       marshalling constant operands to runtime helpers.
int64_t toI64(const il::core::Value &value);

/// @brief Convert a constant VM value into a 64-bit floating point number.
/// @details Mirrors toI64 but produces a double precision result. Integer
///          constants are cast, null pointers yield zero, and unsupported kinds
///          assert during development builds.
/// @pre isConstantScalar(value) must be true.
/// @param value Constant VM value to convert.
/// @return 64-bit floating point representation of @p value.
/// @note This is NOT a general-purpose coercion. It is intended only for
///       marshalling constant operands to runtime helpers.
double toF64(const il::core::Value &value);

void *slotToArgPointer(Slot &slot, il::core::Type::Kind kind);
void *resultBufferFor(il::core::Type::Kind kind, ResultBuffers &buffers);
void assignResult(Slot &slot, il::core::Type::Kind kind, const ResultBuffers &buffers);
std::vector<void *> marshalArguments(const il::runtime::RuntimeSignature &sig,
                                     std::span<Slot> args,
                                     PowStatus &powStatus);
PowTrapOutcome classifyPowTrap(const il::runtime::RuntimeDescriptor &desc,
                               const PowStatus &powStatus,
                               std::span<const Slot> args,
                               const ResultBuffers &buffers);
Slot assignCallResult(const il::runtime::RuntimeSignature &signature, const ResultBuffers &buffers);

//===----------------------------------------------------------------------===//
// Marshalling Validation Helpers
//===----------------------------------------------------------------------===//

/// @brief Result of marshalling validation checks.
struct MarshalValidation
{
    bool ok{true};            ///< True when all checks pass.
    std::string errorMessage; ///< Diagnostic message when validation fails.
};

/// @brief Validate that argument count matches the expected parameter count.
/// @param desc Runtime descriptor describing the expected signature.
/// @param argCount Number of arguments actually supplied.
/// @return Validation result with error message on mismatch.
[[nodiscard]] MarshalValidation validateMarshalArity(const il::runtime::RuntimeDescriptor &desc,
                                                     std::size_t argCount);

/// @brief Validate that argument count matches the expected parameter count.
/// @param sig Runtime signature describing expected parameters.
/// @param argCount Number of arguments actually supplied.
/// @param calleeName Name used in error messages.
/// @return Validation result with error message on mismatch.
[[nodiscard]] MarshalValidation validateMarshalArity(const il::runtime::RuntimeSignature &sig,
                                                     std::size_t argCount,
                                                     std::string_view calleeName);

/// @brief Validate argument count and optionally check for null pointer args.
/// @param desc Runtime descriptor describing the expected signature.
/// @param args Span of argument slots to validate.
/// @param checkNullPointers When true, validates that pointer-typed args are non-null.
/// @return Validation result with error message on failure.
[[nodiscard]] MarshalValidation validateMarshalArgs(const il::runtime::RuntimeDescriptor &desc,
                                                    std::span<const Slot> args,
                                                    bool checkNullPointers = false);

/// @brief Combined marshal and validate: checks arity then builds void** array.
/// @details This is the recommended entry point for marshalling runtime calls.
///          It validates argument count before marshalling to avoid out-of-bounds
///          access, and can optionally validate pointer arguments.
/// @param desc Runtime descriptor for the callee.
/// @param args Argument slots supplied by the caller.
/// @param powStatus [out] Power function trap status tracker.
/// @param validation [out] Validation result; check before using returned vector.
/// @param checkNullPointers When true, validates pointer args are non-null.
/// @return Vector of marshalled argument pointers; empty on validation failure.
[[nodiscard]] std::vector<void *> marshalArgumentsValidated(
    const il::runtime::RuntimeDescriptor &desc,
    std::span<Slot> args,
    PowStatus &powStatus,
    MarshalValidation &validation,
    bool checkNullPointers = false);

} // namespace il::vm
