//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/semantics/ScalarOps.hpp
// Purpose: Shared scalar instruction semantics for all Viper execution engines.
// Key invariants: Functions are pure, deterministic, and never rely on C++ signed
//                 overflow, implementation-defined shifts, or undefined division.
// Ownership: Header-only semantic kernel; owns no state and borrows no VM objects.
// Lifetime: All functions are stateless and safe to call from interpreters, JITs,
//           validators, and conformance tests.
// Links: docs/languages/arithmetic-semantics.md, il/core/FPCast.hpp, vm/IntOpSupport.hpp,
//        bytecode/BytecodeVM.cpp, bytecode/BytecodeVM_threaded.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/FPCast.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

/// @file
/// @brief Pure scalar operation semantics shared by the IL VM and bytecode VM.
/// @details This header centralizes the behavior that used to be hand-maintained
///          in multiple interpreters: checked integer arithmetic, division traps,
///          bounds normalization, narrowing conversions, and checked floating-point
///          casts.  The routines intentionally return small result objects rather
///          than raising VM-specific traps so each execution engine can preserve
///          its own trap transport while sharing the same semantic decisions.

namespace il::semantics {

/// @brief Trap categories produced by the shared scalar semantic kernel.
/// @details The values are VM-neutral.  Interpreters map them to their native
///          trap enums at the call site so this header remains independent from
///          both `il::vm::TrapKind` and `viper::bytecode::TrapKind`.
enum class TrapKind : uint8_t {
    None,             ///< Operation completed normally.
    DivideByZero,     ///< Division or remainder divisor was zero.
    Overflow,         ///< Arithmetic or representability overflow.
    InvalidCast,      ///< Conversion source value is invalid or out of range.
    Bounds,           ///< Bounds check failed.
    InvalidOperation, ///< Operation is unsupported for the requested width.
    DomainError       ///< Semantic domain violation not covered by a narrower kind.
};

/// @brief Integer widths with first-class IL scalar semantics.
/// @details `I1` is used for boolean narrowing and bit-width metadata.  Arithmetic
///          operations that do not define boolean arithmetic promote `I1` to the
///          same behavior as `I64`, matching the historical IL VM dispatch path.
enum class IntWidth : uint8_t {
    I1 = 1,   ///< One-bit boolean storage.
    I16 = 16, ///< Sixteen-bit integer arithmetic.
    I32 = 32, ///< Thirty-two-bit integer arithmetic.
    I64 = 64  ///< Sixty-four-bit integer arithmetic.
};

/// @brief Result wrapper for operations that can trap.
/// @tparam T Value type carried on successful execution.
/// @details A result is successful when @ref trap equals @ref TrapKind::None.  On
///          failure, @ref value is zero-initialized and must not be interpreted as
///          a semantic result.
template <typename T> struct SemanticResult {
    T value{};                         ///< Successful operation result.
    TrapKind trap{TrapKind::None};     ///< Trap category, or None on success.

    /// @brief Test whether the operation completed without trapping.
    /// @return True when @ref trap is @ref TrapKind::None.
    [[nodiscard]] constexpr bool ok() const noexcept {
        return trap == TrapKind::None;
    }
};

/// @brief Build a successful semantic result.
/// @tparam T Value type carried by the result.
/// @param value Result value produced by the operation.
/// @return Successful @ref SemanticResult containing @p value.
template <typename T> [[nodiscard]] constexpr SemanticResult<T> success(T value) noexcept {
    return {value, TrapKind::None};
}

/// @brief Build a trapping semantic result.
/// @tparam T Value type that would have been produced on success.
/// @param trap Trap category describing the failure.
/// @return Failed @ref SemanticResult with a zero-initialized value.
template <typename T> [[nodiscard]] constexpr SemanticResult<T> failure(TrapKind trap) noexcept {
    return {T{}, trap};
}

/// @brief Return the number of bits represented by an integer width.
/// @param width IL integer width tag.
/// @return Bit count for @p width.
[[nodiscard]] constexpr unsigned bitCount(IntWidth width) noexcept {
    return static_cast<unsigned>(width);
}

/// @brief Convert a type bit count into an integer width tag.
/// @details Unknown bit counts conservatively select `I64`, matching existing
///          interpreter fallback behavior for unsupported integer kinds.
/// @param bits Requested bit count.
/// @return Matching @ref IntWidth, or @ref IntWidth::I64 for unknown counts.
[[nodiscard]] constexpr IntWidth widthFromBits(unsigned bits) noexcept {
    switch (bits) {
        case 1:
            return IntWidth::I1;
        case 16:
            return IntWidth::I16;
        case 32:
            return IntWidth::I32;
        case 64:
        default:
            return IntWidth::I64;
    }
}

/// @brief Convert the legacy bytecode width argument into an integer width.
/// @details This mapping is used by checked arithmetic and narrowing bytecodes:
///          0 -> I1, 1 -> I16, 2 -> I32, 3 -> I64.  Values outside the encoding
///          are treated as I64 so malformed but already-loaded modules do not
///          accidentally narrow arithmetic.
/// @param encoded Three-bit width code from bytecode metadata.
/// @return Decoded integer width.
[[nodiscard]] constexpr IntWidth widthFromLegacyEncoding(uint8_t encoded) noexcept {
    switch (encoded) {
        case 0:
            return IntWidth::I1;
        case 1:
            return IntWidth::I16;
        case 2:
            return IntWidth::I32;
        case 3:
        default:
            return IntWidth::I64;
    }
}

/// @brief Encode an integer width using the legacy bytecode width mapping.
/// @param width Integer width to encode.
/// @return 0 for I1, 1 for I16, 2 for I32, and 3 for I64.
[[nodiscard]] constexpr uint8_t encodeLegacyWidth(IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I1:
            return 0;
        case IntWidth::I16:
            return 1;
        case IntWidth::I32:
            return 2;
        case IntWidth::I64:
        default:
            return 3;
    }
}

/// @brief Return the signed minimum value representable by a width.
/// @details `I1` is interpreted as boolean storage and returns zero.
/// @param width Integer width to inspect.
/// @return Minimum signed value for the width.
[[nodiscard]] constexpr int64_t signedMin(IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I1:
            return 0;
        case IntWidth::I16:
            return std::numeric_limits<int16_t>::min();
        case IntWidth::I32:
            return std::numeric_limits<int32_t>::min();
        case IntWidth::I64:
        default:
            return std::numeric_limits<int64_t>::min();
    }
}

/// @brief Return the signed maximum value representable by a width.
/// @details `I1` is interpreted as boolean storage and returns one.
/// @param width Integer width to inspect.
/// @return Maximum signed value for the width.
[[nodiscard]] constexpr int64_t signedMax(IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I1:
            return 1;
        case IntWidth::I16:
            return std::numeric_limits<int16_t>::max();
        case IntWidth::I32:
            return std::numeric_limits<int32_t>::max();
        case IntWidth::I64:
        default:
            return std::numeric_limits<int64_t>::max();
    }
}

/// @brief Return the unsigned maximum value representable by a width.
/// @param width Integer width to inspect.
/// @return Maximum unsigned value for @p width.
[[nodiscard]] constexpr uint64_t unsignedMax(IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I1:
            return 1;
        case IntWidth::I16:
            return std::numeric_limits<uint16_t>::max();
        case IntWidth::I32:
            return std::numeric_limits<uint32_t>::max();
        case IntWidth::I64:
        default:
            return std::numeric_limits<uint64_t>::max();
    }
}

/// @brief Test whether a signed value fits within a target width.
/// @param value Signed value to test.
/// @param width Target signed width.
/// @return True if @p value is representable by @p width.
[[nodiscard]] constexpr bool fitsSigned(int64_t value, IntWidth width) noexcept {
    return value >= signedMin(width) && value <= signedMax(width);
}

/// @brief Test whether an unsigned value fits within a target width.
/// @param value Unsigned value to test.
/// @param width Target unsigned width.
/// @return True if @p value is representable by @p width.
[[nodiscard]] constexpr bool fitsUnsigned(uint64_t value, IntWidth width) noexcept {
    return value <= unsignedMax(width);
}

/// @brief Perform checked signed addition for a concrete signed integer type.
/// @tparam T Signed integer type used for the operation.
/// @param lhs Left operand already converted to @p T.
/// @param rhs Right operand already converted to @p T.
/// @param out Receives the sum when no overflow occurs.
/// @return True when the addition overflows @p T.
template <typename T>
[[nodiscard]] constexpr bool checkedAddTyped(T lhs, T rhs, T &out) noexcept {
    static_assert(std::is_signed_v<T>, "checkedAddTyped requires a signed integer type");
#if defined(__GNUC__) || defined(__clang__)
    if (!std::is_constant_evaluated())
        return __builtin_add_overflow(lhs, rhs, &out);
#endif
    if ((rhs > 0 && lhs > std::numeric_limits<T>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<T>::min() - rhs)) {
        out = T{};
        return true;
    }
    out = static_cast<T>(lhs + rhs);
    return false;
}

/// @brief Perform checked signed subtraction for a concrete signed integer type.
/// @tparam T Signed integer type used for the operation.
/// @param lhs Left operand already converted to @p T.
/// @param rhs Right operand already converted to @p T.
/// @param out Receives the difference when no overflow occurs.
/// @return True when the subtraction overflows @p T.
template <typename T>
[[nodiscard]] constexpr bool checkedSubTyped(T lhs, T rhs, T &out) noexcept {
    static_assert(std::is_signed_v<T>, "checkedSubTyped requires a signed integer type");
#if defined(__GNUC__) || defined(__clang__)
    if (!std::is_constant_evaluated())
        return __builtin_sub_overflow(lhs, rhs, &out);
#endif
    if ((rhs < 0 && lhs > std::numeric_limits<T>::max() + rhs) ||
        (rhs > 0 && lhs < std::numeric_limits<T>::min() + rhs)) {
        out = T{};
        return true;
    }
    out = static_cast<T>(lhs - rhs);
    return false;
}

/// @brief Perform checked signed multiplication for a concrete signed integer type.
/// @tparam T Signed integer type used for the operation.
/// @param lhs Left operand already converted to @p T.
/// @param rhs Right operand already converted to @p T.
/// @param out Receives the product when no overflow occurs.
/// @return True when the multiplication overflows @p T.
template <typename T>
[[nodiscard]] constexpr bool checkedMulTyped(T lhs, T rhs, T &out) noexcept {
    static_assert(std::is_signed_v<T>, "checkedMulTyped requires a signed integer type");
#if defined(__GNUC__) || defined(__clang__)
    if (!std::is_constant_evaluated())
        return __builtin_mul_overflow(lhs, rhs, &out);
#endif
    if constexpr (sizeof(T) < sizeof(int64_t)) {
        const int64_t product = static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs);
        if (product < static_cast<int64_t>(std::numeric_limits<T>::min()) ||
            product > static_cast<int64_t>(std::numeric_limits<T>::max())) {
            out = T{};
            return true;
        }
        out = static_cast<T>(product);
        return false;
    } else {
        if (lhs == 0 || rhs == 0) {
            out = 0;
            return false;
        }
        if (lhs == -1 && rhs == std::numeric_limits<T>::min()) {
            out = T{};
            return true;
        }
        if (rhs == -1 && lhs == std::numeric_limits<T>::min()) {
            out = T{};
            return true;
        }
        const T max = std::numeric_limits<T>::max();
        const T min = std::numeric_limits<T>::min();
        if ((lhs > 0 && rhs > 0 && lhs > max / rhs) ||
            (lhs > 0 && rhs < 0 && rhs < min / lhs) ||
            (lhs < 0 && rhs > 0 && lhs < min / rhs) ||
            (lhs < 0 && rhs < 0 && lhs < max / rhs)) {
            out = T{};
            return true;
        }
        out = static_cast<T>(lhs * rhs);
        return false;
    }
}

/// @brief Execute a typed checked signed binary operation.
/// @tparam T Signed integer type selected by IL width metadata.
/// @tparam Op Callable implementing a checked typed operation.
/// @param lhs Raw slot left operand.
/// @param rhs Raw slot right operand.
/// @param op Operation callable returning true on overflow.
/// @return Operation result or an overflow trap.
template <typename T, typename Op>
[[nodiscard]] constexpr SemanticResult<int64_t> checkedTypedBinary(int64_t lhs,
                                                                   int64_t rhs,
                                                                   Op op) noexcept {
    T typedLhs = static_cast<T>(lhs);
    T typedRhs = static_cast<T>(rhs);
    T typedResult{};
    if (op(typedLhs, typedRhs, typedResult))
        return failure<int64_t>(TrapKind::Overflow);
    return success<int64_t>(static_cast<int64_t>(typedResult));
}

/// @brief Execute checked signed addition at a requested IL width.
/// @details Widths I16, I32, and I64 use typed arithmetic.  I1 falls back to I64
///          because the IL does not define boolean addition overflow semantics.
/// @param lhs Raw slot left operand.
/// @param rhs Raw slot right operand.
/// @param width Integer width controlling overflow.
/// @return Sum or an overflow trap.
[[nodiscard]] constexpr SemanticResult<int64_t> checkedAdd(int64_t lhs,
                                                           int64_t rhs,
                                                           IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I16:
            return checkedTypedBinary<int16_t>(lhs, rhs, checkedAddTyped<int16_t>);
        case IntWidth::I32:
            return checkedTypedBinary<int32_t>(lhs, rhs, checkedAddTyped<int32_t>);
        case IntWidth::I1:
        case IntWidth::I64:
        default:
            return checkedTypedBinary<int64_t>(lhs, rhs, checkedAddTyped<int64_t>);
    }
}

/// @brief Execute checked signed subtraction at a requested IL width.
/// @details Widths I16, I32, and I64 use typed arithmetic.  I1 falls back to I64
///          because the IL does not define boolean subtraction overflow semantics.
/// @param lhs Raw slot left operand.
/// @param rhs Raw slot right operand.
/// @param width Integer width controlling overflow.
/// @return Difference or an overflow trap.
[[nodiscard]] constexpr SemanticResult<int64_t> checkedSub(int64_t lhs,
                                                           int64_t rhs,
                                                           IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I16:
            return checkedTypedBinary<int16_t>(lhs, rhs, checkedSubTyped<int16_t>);
        case IntWidth::I32:
            return checkedTypedBinary<int32_t>(lhs, rhs, checkedSubTyped<int32_t>);
        case IntWidth::I1:
        case IntWidth::I64:
        default:
            return checkedTypedBinary<int64_t>(lhs, rhs, checkedSubTyped<int64_t>);
    }
}

/// @brief Execute checked signed multiplication at a requested IL width.
/// @details Widths I16, I32, and I64 use typed arithmetic.  I1 falls back to I64
///          because the IL does not define boolean multiplication overflow semantics.
/// @param lhs Raw slot left operand.
/// @param rhs Raw slot right operand.
/// @param width Integer width controlling overflow.
/// @return Product or an overflow trap.
[[nodiscard]] constexpr SemanticResult<int64_t> checkedMul(int64_t lhs,
                                                           int64_t rhs,
                                                           IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I16:
            return checkedTypedBinary<int16_t>(lhs, rhs, checkedMulTyped<int16_t>);
        case IntWidth::I32:
            return checkedTypedBinary<int32_t>(lhs, rhs, checkedMulTyped<int32_t>);
        case IntWidth::I1:
        case IntWidth::I64:
        default:
            return checkedTypedBinary<int64_t>(lhs, rhs, checkedMulTyped<int64_t>);
    }
}

/// @brief Add two signed values with two's-complement wrapping semantics.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Low 64 bits of the sum reinterpreted as signed storage.
[[nodiscard]] constexpr int64_t wrapAdd(int64_t lhs, int64_t rhs) noexcept {
    return std::bit_cast<int64_t>(std::bit_cast<uint64_t>(lhs) + std::bit_cast<uint64_t>(rhs));
}

/// @brief Subtract two signed values with two's-complement wrapping semantics.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Low 64 bits of the difference reinterpreted as signed storage.
[[nodiscard]] constexpr int64_t wrapSub(int64_t lhs, int64_t rhs) noexcept {
    return std::bit_cast<int64_t>(std::bit_cast<uint64_t>(lhs) - std::bit_cast<uint64_t>(rhs));
}

/// @brief Multiply two signed values with two's-complement wrapping semantics.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Low 64 bits of the product reinterpreted as signed storage.
[[nodiscard]] constexpr int64_t wrapMul(int64_t lhs, int64_t rhs) noexcept {
    return std::bit_cast<int64_t>(std::bit_cast<uint64_t>(lhs) * std::bit_cast<uint64_t>(rhs));
}

/// @brief Shift left using masked, unsigned shift semantics.
/// @param value Value to shift.
/// @param shift Shift amount; only the low six bits are observed.
/// @return Shifted bit pattern stored as signed 64-bit data.
[[nodiscard]] constexpr int64_t shiftLeft(int64_t value, int64_t shift) noexcept {
    return std::bit_cast<int64_t>(std::bit_cast<uint64_t>(value)
                                  << (static_cast<uint64_t>(shift) & 63U));
}

/// @brief Logical shift right using masked, unsigned shift semantics.
/// @param value Value to shift.
/// @param shift Shift amount; only the low six bits are observed.
/// @return Shifted bit pattern stored as signed 64-bit data.
[[nodiscard]] constexpr int64_t logicalShiftRight(int64_t value, int64_t shift) noexcept {
    return std::bit_cast<int64_t>(std::bit_cast<uint64_t>(value)
                                  >> (static_cast<uint64_t>(shift) & 63U));
}

/// @brief Arithmetic shift right with deterministic sign extension.
/// @details The implementation avoids implementation-defined signed right shift
///          by operating on the unsigned bit pattern and manually filling sign bits.
/// @param value Value to shift.
/// @param shift Shift amount; only the low six bits are observed.
/// @return Shifted value with sign bits extended.
[[nodiscard]] constexpr int64_t arithmeticShiftRight(int64_t value, int64_t shift) noexcept {
    const unsigned amount = static_cast<unsigned>(static_cast<uint64_t>(shift) & 63U);
    if (amount == 0)
        return value;
    const uint64_t bits = std::bit_cast<uint64_t>(value);
    uint64_t shifted = bits >> amount;
    if ((bits & (UINT64_C(1) << 63)) != 0) {
        shifted |= (~UINT64_C(0)) << (64U - amount);
    }
    return std::bit_cast<int64_t>(shifted);
}

/// @brief Execute typed signed division without host undefined behavior.
/// @tparam T Signed integer type selected by IL width metadata.
/// @param lhs Raw slot dividend.
/// @param rhs Raw slot divisor.
/// @return Quotient, divide-by-zero trap, or overflow trap for MIN / -1.
template <typename T>
[[nodiscard]] constexpr SemanticResult<int64_t> signedDivTyped(int64_t lhs,
                                                               int64_t rhs) noexcept {
    T typedLhs = static_cast<T>(lhs);
    T typedRhs = static_cast<T>(rhs);
    if (typedRhs == 0)
        return failure<int64_t>(TrapKind::DivideByZero);
    if (typedLhs == std::numeric_limits<T>::min() && typedRhs == static_cast<T>(-1))
        return failure<int64_t>(TrapKind::Overflow);
    return success<int64_t>(static_cast<int64_t>(static_cast<T>(typedLhs / typedRhs)));
}

/// @brief Execute signed division at a requested IL width.
/// @details I1 falls back to I64 because boolean division is not defined by IL.
/// @param lhs Raw slot dividend.
/// @param rhs Raw slot divisor.
/// @param width Integer width controlling operand narrowing.
/// @return Quotient or trap.
[[nodiscard]] constexpr SemanticResult<int64_t> signedDiv(int64_t lhs,
                                                          int64_t rhs,
                                                          IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I16:
            return signedDivTyped<int16_t>(lhs, rhs);
        case IntWidth::I32:
            return signedDivTyped<int32_t>(lhs, rhs);
        case IntWidth::I1:
        case IntWidth::I64:
        default:
            return signedDivTyped<int64_t>(lhs, rhs);
    }
}

/// @brief Execute typed signed remainder without host undefined behavior.
/// @details The MIN % -1 case is defined as zero so checked and unchecked IL
///          remainder are deterministic and do not trap for that pair.
/// @tparam T Signed integer type selected by IL width metadata.
/// @param lhs Raw slot dividend.
/// @param rhs Raw slot divisor.
/// @return Remainder, or divide-by-zero trap.
template <typename T>
[[nodiscard]] constexpr SemanticResult<int64_t> signedRemTyped(int64_t lhs,
                                                               int64_t rhs) noexcept {
    T typedLhs = static_cast<T>(lhs);
    T typedRhs = static_cast<T>(rhs);
    if (typedRhs == 0)
        return failure<int64_t>(TrapKind::DivideByZero);
    if (typedLhs == std::numeric_limits<T>::min() && typedRhs == static_cast<T>(-1))
        return success<int64_t>(0);
    const int64_t wideLhs = static_cast<int64_t>(typedLhs);
    const int64_t wideRhs = static_cast<int64_t>(typedRhs);
    const int64_t quotient = wideLhs / wideRhs;
    const int64_t remainder = wideLhs - quotient * wideRhs;
    return success<int64_t>(static_cast<int64_t>(static_cast<T>(remainder)));
}

/// @brief Execute signed remainder at a requested IL width.
/// @details I1 falls back to I64 because boolean remainder is not defined by IL.
/// @param lhs Raw slot dividend.
/// @param rhs Raw slot divisor.
/// @param width Integer width controlling operand narrowing.
/// @return Remainder or trap.
[[nodiscard]] constexpr SemanticResult<int64_t> signedRem(int64_t lhs,
                                                          int64_t rhs,
                                                          IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I16:
            return signedRemTyped<int16_t>(lhs, rhs);
        case IntWidth::I32:
            return signedRemTyped<int32_t>(lhs, rhs);
        case IntWidth::I1:
        case IntWidth::I64:
        default:
            return signedRemTyped<int64_t>(lhs, rhs);
    }
}

/// @brief Execute unsigned division without host undefined behavior.
/// @param lhs Raw slot dividend bit pattern.
/// @param rhs Raw slot divisor bit pattern.
/// @return Quotient bit pattern or divide-by-zero trap.
[[nodiscard]] constexpr SemanticResult<int64_t> unsignedDiv(int64_t lhs, int64_t rhs) noexcept {
    const uint64_t divisor = std::bit_cast<uint64_t>(rhs);
    if (divisor == 0)
        return failure<int64_t>(TrapKind::DivideByZero);
    return success<int64_t>(
        std::bit_cast<int64_t>(std::bit_cast<uint64_t>(lhs) / divisor));
}

/// @brief Execute unsigned remainder without host undefined behavior.
/// @param lhs Raw slot dividend bit pattern.
/// @param rhs Raw slot divisor bit pattern.
/// @return Remainder bit pattern or divide-by-zero trap.
[[nodiscard]] constexpr SemanticResult<int64_t> unsignedRem(int64_t lhs, int64_t rhs) noexcept {
    const uint64_t divisor = std::bit_cast<uint64_t>(rhs);
    if (divisor == 0)
        return failure<int64_t>(TrapKind::DivideByZero);
    return success<int64_t>(
        std::bit_cast<int64_t>(std::bit_cast<uint64_t>(lhs) % divisor));
}

/// @brief Execute checked signed negation.
/// @param value Operand to negate.
/// @return Negated value or overflow when @p value is INT64_MIN.
[[nodiscard]] constexpr SemanticResult<int64_t> negate(int64_t value) noexcept {
    if (value == std::numeric_limits<int64_t>::min())
        return failure<int64_t>(TrapKind::Overflow);
    return success<int64_t>(-value);
}

/// @brief Execute signed narrowing with IL trap semantics.
/// @param value Signed source value.
/// @param width Target integer width.
/// @return Narrowed storage value or InvalidCast when out of range.
[[nodiscard]] constexpr SemanticResult<int64_t> signedNarrow(int64_t value,
                                                             IntWidth width) noexcept {
    if (!fitsSigned(value, width))
        return failure<int64_t>(TrapKind::InvalidCast);
    switch (width) {
        case IntWidth::I1:
            return success<int64_t>(value & 1);
        case IntWidth::I16:
            return success<int64_t>(static_cast<int64_t>(static_cast<int16_t>(value)));
        case IntWidth::I32:
            return success<int64_t>(static_cast<int64_t>(static_cast<int32_t>(value)));
        case IntWidth::I64:
        default:
            return success<int64_t>(value);
    }
}

/// @brief Execute unsigned narrowing with IL trap semantics.
/// @param value Unsigned source bit pattern.
/// @param width Target integer width.
/// @return Narrowed storage value or InvalidCast when out of range.
[[nodiscard]] constexpr SemanticResult<int64_t> unsignedNarrow(uint64_t value,
                                                               IntWidth width) noexcept {
    if (!fitsUnsigned(value, width))
        return failure<int64_t>(TrapKind::InvalidCast);
    switch (width) {
        case IntWidth::I1:
            return success<int64_t>(static_cast<int64_t>(value & 1U));
        case IntWidth::I16:
            return success<int64_t>(static_cast<int64_t>(static_cast<uint16_t>(value)));
        case IntWidth::I32:
            return success<int64_t>(static_cast<int64_t>(static_cast<uint32_t>(value)));
        case IntWidth::I64:
        default:
            return success<int64_t>(std::bit_cast<int64_t>(value));
    }
}

/// @brief Execute a typed bounds check and normalize a successful index.
/// @tparam T Signed integer type selected by IL width metadata.
/// @param idx Raw slot index.
/// @param lo Raw slot inclusive lower bound.
/// @param hi Raw slot exclusive upper bound.
/// @return Normalized `idx - lo` offset, or Bounds when outside `[lo, hi)`.
template <typename T>
[[nodiscard]] constexpr SemanticResult<int64_t> boundsCheckTyped(int64_t idx,
                                                                 int64_t lo,
                                                                 int64_t hi) noexcept {
    const T typedIdx = static_cast<T>(idx);
    const T typedLo = static_cast<T>(lo);
    const T typedHi = static_cast<T>(hi);
    if (typedIdx < typedLo || typedIdx >= typedHi)
        return failure<int64_t>(TrapKind::Bounds);
    using UnsignedT = std::make_unsigned_t<T>;
    const UnsignedT normalized = static_cast<UnsignedT>(typedIdx) - static_cast<UnsignedT>(typedLo);
    if (static_cast<uint64_t>(normalized) >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return failure<int64_t>(TrapKind::Bounds);
    }
    return success<int64_t>(static_cast<int64_t>(normalized));
}

/// @brief Execute an IL bounds check and normalize a successful index.
/// @details The range is half-open: `lo <= idx < hi`.  On success the returned
///          value is the normalized offset `idx - lo`, matching the IL VM's
///          long-standing `idx.chk` contract.
/// @param idx Raw slot index.
/// @param lo Raw slot inclusive lower bound.
/// @param hi Raw slot exclusive upper bound.
/// @param width Integer width controlling signed comparison.
/// @return Normalized offset, or Bounds when the check fails.
[[nodiscard]] constexpr SemanticResult<int64_t> boundsCheck(int64_t idx,
                                                            int64_t lo,
                                                            int64_t hi,
                                                            IntWidth width) noexcept {
    switch (width) {
        case IntWidth::I16:
            return boundsCheckTyped<int16_t>(idx, lo, hi);
        case IntWidth::I32:
            return boundsCheckTyped<int32_t>(idx, lo, hi);
        case IntWidth::I1:
        case IntWidth::I64:
        default:
            return boundsCheckTyped<int64_t>(idx, lo, hi);
    }
}

/// @brief Convert an f64 to i64 using truncation toward zero.
/// @details This implements the unchecked `f64 -> i64` conversion trap behavior:
///          NaN and infinities are InvalidCast, and magnitudes outside the signed
///          64-bit range are Overflow.  Finite in-range values are truncated.
/// @param value Floating-point source operand.
/// @return Truncated integer result or conversion trap.
[[nodiscard]] inline SemanticResult<int64_t> truncF64ToI64(double value) noexcept {
    if (!std::isfinite(value))
        return failure<int64_t>(TrapKind::InvalidCast);
    if (value < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        value >= 9223372036854775808.0) {
        return failure<int64_t>(TrapKind::Overflow);
    }
    return success<int64_t>(static_cast<int64_t>(value));
}

/// @brief Map the shared f64 checked-cast result to scalar semantic traps.
/// @param result Result returned by `il::core` floating-point cast helpers.
/// @return Result value or a converted trap category.
[[nodiscard]] inline SemanticResult<int64_t>
fromCheckedFpCast(il::core::CheckedFPCastResult result) noexcept {
    switch (result.failure) {
        case il::core::CheckedFPCastFailure::None:
            return success<int64_t>(result.value);
        case il::core::CheckedFPCastFailure::Invalid:
            return failure<int64_t>(TrapKind::InvalidCast);
        case il::core::CheckedFPCastFailure::Overflow:
        default:
            return failure<int64_t>(TrapKind::Overflow);
    }
}

/// @brief Convert f64 to signed integer using round-to-nearest, ties-to-even.
/// @param value Floating-point source operand.
/// @param width Target signed integer width.
/// @return Rounded integer result or conversion trap.
[[nodiscard]] inline SemanticResult<int64_t> fpToSiRte(double value, IntWidth width) noexcept {
    return fromCheckedFpCast(il::core::checkedFpToSiRte(value, static_cast<int>(bitCount(width))));
}

/// @brief Convert f64 to unsigned integer using round-to-nearest, ties-to-even.
/// @details The result is returned as the signed slot storage bit pattern, so
///          successful 64-bit unsigned conversions preserve values >= 2^63.
/// @param value Floating-point source operand.
/// @param width Target unsigned integer width.
/// @return Rounded integer storage value or conversion trap.
[[nodiscard]] inline SemanticResult<int64_t> fpToUiRte(double value, IntWidth width) noexcept {
    return fromCheckedFpCast(il::core::checkedFpToUiRte(value, static_cast<int>(bitCount(width))));
}

} // namespace il::semantics
