//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/IntOpSupport.hpp
// Purpose: Shared helpers for integer opcode handlers, covering trap dispatch and
// Key invariants: Helpers operate on canonicalised Slot values and honour IL trap
// Ownership/Lifetime: To be documented.
// Links: docs/il-guide.md#reference §Integer Arithmetic, §Bitwise and Shifts,
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"

#include <cstdint>
#include <limits>
#include <utility>
#include <type_traits>

// Note: MSVC overflow checking is handled by OpHandlerUtils.hpp
// which provides ops::checked_add/sub/mul that work on all platforms.

namespace il::vm::detail::integer
{
/// @brief Emit a trap with context from the current instruction and frame.
/// @details Thin wrapper that formats trap metadata for RuntimeBridge, ensuring
///          all instruction traps include function, block, and source location
///          information for better diagnostics.
/// @param kind Classification of the trap (e.g., overflow, divide-by-zero).
/// @param message Human-readable description of the failure.
/// @param in Instruction that triggered the trap.
/// @param fr Active frame containing function metadata.
/// @param bb Current basic block pointer, may be null.
inline void emitTrap(TrapKind kind,
                     const char *message,
                     const il::core::Instr &in,
                     Frame &fr,
                     const il::core::BasicBlock *bb)
{
    RuntimeBridge::trap(kind, message, in.loc, fr.func->name, bb ? bb->label : "");
}

template <typename T, typename OverflowOp>
void applyOverflowingBinary(const il::core::Instr &in,
                            Frame &fr,
                            const il::core::BasicBlock *bb,
                            Slot &out,
                            const Slot &lhsVal,
                            const Slot &rhsVal,
                            const char *trapMessage,
                            OverflowOp &&op)
{
    T lhs = static_cast<T>(lhsVal.i64);
    T rhs = static_cast<T>(rhsVal.i64);
    T result{};
    if (op(lhs, rhs, &result))
    {
        emitTrap(TrapKind::Overflow, trapMessage, in, fr, bb);
        return;
    }
    out.i64 = static_cast<int64_t>(result);
}

template <typename OverflowOp>
void dispatchOverflowingBinary(const il::core::Instr &in,
                               Frame &fr,
                               const il::core::BasicBlock *bb,
                               Slot &out,
                               const Slot &lhsVal,
                               const Slot &rhsVal,
                               const char *trapMessage,
                               OverflowOp overflowOp)
{
    switch (in.type.kind)
    {
        case il::core::Type::Kind::I16:
            applyOverflowingBinary<int16_t>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage, overflowOp);
            break;
        case il::core::Type::Kind::I32:
            applyOverflowingBinary<int32_t>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage, overflowOp);
            break;
        case il::core::Type::Kind::I64:
        default:
            applyOverflowingBinary<int64_t>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage, overflowOp);
            break;
    }
}

template <typename T>
void applySignedDiv(const il::core::Instr &in,
                    Frame &fr,
                    const il::core::BasicBlock *bb,
                    Slot &out,
                    const Slot &lhsVal,
                    const Slot &rhsVal)
{
    const T lhs = static_cast<T>(lhsVal.i64);
    const T rhs = static_cast<T>(rhsVal.i64);
    if (rhs == 0)
    {
        emitTrap(TrapKind::DivideByZero, "divide by zero in sdiv", in, fr, bb);
        return;
    }
    if (lhs == std::numeric_limits<T>::min() && rhs == static_cast<T>(-1))
    {
        emitTrap(TrapKind::Overflow, "integer overflow in sdiv", in, fr, bb);
        return;
    }
    out.i64 = static_cast<int64_t>(static_cast<T>(lhs / rhs));
}

template <typename T>
void applySignedRem(const il::core::Instr &in,
                    Frame &fr,
                    const il::core::BasicBlock *bb,
                    Slot &out,
                    const Slot &lhsVal,
                    const Slot &rhsVal)
{
    const T lhs = static_cast<T>(lhsVal.i64);
    const T rhs = static_cast<T>(rhsVal.i64);
    if (rhs == 0)
    {
        emitTrap(TrapKind::DivideByZero, "divide by zero in srem", in, fr, bb);
        return;
    }

    const int64_t wideLhs = static_cast<int64_t>(lhs);
    const int64_t wideRhs = static_cast<int64_t>(rhs);
    const int64_t quotient = wideLhs / wideRhs;
    const int64_t remainder = wideLhs - quotient * wideRhs;
    out.i64 = static_cast<int64_t>(static_cast<T>(remainder));
}

template <typename T>
void applyCheckedDiv(const il::core::Instr &in,
                     Frame &fr,
                     const il::core::BasicBlock *bb,
                     Slot &out,
                     const Slot &lhsVal,
                     const Slot &rhsVal)
{
    T lhs = static_cast<T>(lhsVal.i64);
    T rhs = static_cast<T>(rhsVal.i64);
    if (rhs == 0)
    {
        emitTrap(TrapKind::DivideByZero, "divide by zero in sdiv.chk0", in, fr, bb);
        return;
    }
    if (lhs == std::numeric_limits<T>::min() && rhs == static_cast<T>(-1))
    {
        emitTrap(TrapKind::Overflow, "integer overflow in sdiv.chk0", in, fr, bb);
        return;
    }
    out.i64 = static_cast<int64_t>(static_cast<T>(lhs / rhs));
}

template <typename T>
void applyCheckedRem(const il::core::Instr &in,
                     Frame &fr,
                     const il::core::BasicBlock *bb,
                     Slot &out,
                     const Slot &lhsVal,
                     const Slot &rhsVal)
{
    T lhs = static_cast<T>(lhsVal.i64);
    T rhs = static_cast<T>(rhsVal.i64);
    if (rhs == 0)
    {
        emitTrap(TrapKind::DivideByZero, "divide by zero in srem.chk0", in, fr, bb);
        return;
    }
    if (lhs == std::numeric_limits<T>::min() && rhs == static_cast<T>(-1))
    {
        out.i64 = 0;
        return;
    }
    const int64_t wideLhs = static_cast<int64_t>(lhs);
    const int64_t wideRhs = static_cast<int64_t>(rhs);
    const int64_t quotient = wideLhs / wideRhs;
    const int64_t remainder = wideLhs - quotient * wideRhs;
    out.i64 = static_cast<int64_t>(static_cast<T>(remainder));
}

using CheckedSignedBinaryFn = void (*)(const il::core::Instr &,
                                       Frame &,
                                       const il::core::BasicBlock *,
                                       Slot &,
                                       const Slot &,
                                       const Slot &);

template <CheckedSignedBinaryFn ApplyI16,
          CheckedSignedBinaryFn ApplyI32,
          CheckedSignedBinaryFn ApplyI64>
void dispatchCheckedSignedBinary(const il::core::Instr &in,
                                 Frame &fr,
                                 const il::core::BasicBlock *bb,
                                 Slot &out,
                                 const Slot &lhsVal,
                                 const Slot &rhsVal)
{
    switch (in.type.kind)
    {
        case il::core::Type::Kind::I16:
            ApplyI16(in, fr, bb, out, lhsVal, rhsVal);
            break;
        case il::core::Type::Kind::I32:
            ApplyI32(in, fr, bb, out, lhsVal, rhsVal);
            break;
        case il::core::Type::Kind::I64:
        default:
            ApplyI64(in, fr, bb, out, lhsVal, rhsVal);
            break;
    }
}

template <typename T>
[[nodiscard]] std::pair<bool, int64_t> performBoundsCheck(const Slot &idxSlot,
                                                          const Slot &loSlot,
                                                          const Slot &hiSlot)
{
    const auto idx = static_cast<T>(idxSlot.i64);
    const auto lo = static_cast<T>(loSlot.i64);
    const auto hi = static_cast<T>(hiSlot.i64);
    if (idx < lo || idx >= hi)
    {
        return {false, static_cast<int64_t>(idx)};
    }
    return {true, static_cast<int64_t>(idx)};
}

/// @brief Check whether a signed 64-bit value fits in a narrower signed type.
/// @tparam NarrowT Target narrow signed integer type.
/// @param value Value to check.
/// @return True if value fits within NarrowT's range.
template <typename NarrowT> [[nodiscard]] constexpr bool fitsSignedRange(int64_t value) noexcept
{
    return value >= static_cast<int64_t>(std::numeric_limits<NarrowT>::min()) &&
           value <= static_cast<int64_t>(std::numeric_limits<NarrowT>::max());
}

template <typename ComputeOp>
void applyUnsignedDivOrRem(const il::core::Instr &in,
                           Frame &fr,
                           const il::core::BasicBlock *bb,
                           Slot &out,
                           const Slot &lhsVal,
                           const Slot &rhsVal,
                           const char *trapMessage,
                           ComputeOp compute)
{
    const auto divisor = static_cast<uint64_t>(rhsVal.i64);
    if (divisor == 0)
    {
        emitTrap(TrapKind::DivideByZero, trapMessage, in, fr, bb);
        return;
    }

    const auto dividend = static_cast<uint64_t>(lhsVal.i64);
    out.i64 = static_cast<int64_t>(compute(dividend, divisor));
}

/// @brief Check whether an unsigned 64-bit value fits in a narrower unsigned type.
/// @tparam NarrowT Target narrow unsigned integer type.
/// @param value Value to check.
/// @return True if value fits within NarrowT's range.
template <typename NarrowT> [[nodiscard]] constexpr bool fitsUnsignedRange(uint64_t value) noexcept
{
    return value <= static_cast<uint64_t>(std::numeric_limits<NarrowT>::max());
}

// ============================================================================
// Optimized Integer Operation Helpers (CRITICAL-4 / HIGH-4 Optimization)
// ============================================================================
// These helpers eliminate lambda captures and reduce type dispatch overhead
// by using function pointers and explicit type parameters.

/// @brief Function pointer type for overflow-checking binary operations.
/// @tparam T Integer type to operate on.
/// @details Returns true on overflow, result is written to *out.
template <typename T> using OverflowCheckFn = bool (*)(T lhs, T rhs, T *out);

/// @brief Stateless overflow-checking add function for use as function pointer.
/// @details Delegates to ops::checked_add which handles MSVC portability.
template <typename T> inline bool overflowAdd(T lhs, T rhs, T *out)
{
    return ops::checked_add(lhs, rhs, out);
}

/// @brief Stateless overflow-checking sub function for use as function pointer.
/// @details Delegates to ops::checked_sub which handles MSVC portability.
template <typename T> inline bool overflowSub(T lhs, T rhs, T *out)
{
    return ops::checked_sub(lhs, rhs, out);
}

/// @brief Stateless overflow-checking mul function for use as function pointer.
/// @details Delegates to ops::checked_mul which handles MSVC portability.
template <typename T> inline bool overflowMul(T lhs, T rhs, T *out)
{
    return ops::checked_mul(lhs, rhs, out);
}

/// @brief Apply an overflow-checking binary operation for a specific type.
/// @tparam T Integer type (int16_t, int32_t, int64_t).
/// @tparam OverflowFn Function pointer for the overflow-checking operation.
/// @param in Instruction being executed.
/// @param fr Active frame.
/// @param bb Current basic block.
/// @param out Output slot.
/// @param lhsVal Left operand slot.
/// @param rhsVal Right operand slot.
/// @param trapMessage Message for overflow trap.
/// @details Uses function pointer instead of lambda to avoid closure allocation.
template <typename T, OverflowCheckFn<T> OverflowFn>
inline void applyOverflowingBinaryDirect(const il::core::Instr &in,
                                         Frame &fr,
                                         const il::core::BasicBlock *bb,
                                         Slot &out,
                                         const Slot &lhsVal,
                                         const Slot &rhsVal,
                                         const char *trapMessage)
{
    T lhs = static_cast<T>(lhsVal.i64);
    T rhs = static_cast<T>(rhsVal.i64);
    T result{};
    if (OverflowFn(lhs, rhs, &result))
    {
        emitTrap(TrapKind::Overflow, trapMessage, in, fr, bb);
        return;
    }
    out.i64 = static_cast<int64_t>(result);
}

/// @brief Dispatch an overflow-checking binary operation based on type kind.
/// @tparam OverflowFn16 Function pointer for int16_t overflow check.
/// @tparam OverflowFn32 Function pointer for int32_t overflow check.
/// @tparam OverflowFn64 Function pointer for int64_t overflow check.
/// @details Uses template function pointers instead of lambdas for efficiency.
template <OverflowCheckFn<int16_t> OverflowFn16,
          OverflowCheckFn<int32_t> OverflowFn32,
          OverflowCheckFn<int64_t> OverflowFn64>
inline void dispatchOverflowingBinaryDirect(const il::core::Instr &in,
                                            Frame &fr,
                                            const il::core::BasicBlock *bb,
                                            Slot &out,
                                            const Slot &lhsVal,
                                            const Slot &rhsVal,
                                            const char *trapMessage)
{
    switch (in.type.kind)
    {
        case il::core::Type::Kind::I16:
            applyOverflowingBinaryDirect<int16_t, OverflowFn16>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage);
            break;
        case il::core::Type::Kind::I32:
            applyOverflowingBinaryDirect<int32_t, OverflowFn32>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage);
            break;
        case il::core::Type::Kind::I64:
        default:
            applyOverflowingBinaryDirect<int64_t, OverflowFn64>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage);
            break;
    }
}

/// @brief Compute functor for overflow-checking addition (CRITICAL-4 optimization).
/// @details Stateless functor that can be passed without creating a closure.
struct OverflowAddOp
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;
    const char *trapMessage;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        dispatchOverflowingBinaryDirect<&overflowAdd<int16_t>,
                                        &overflowAdd<int32_t>,
                                        &overflowAdd<int64_t>>(
            in, fr, bb, out, lhsVal, rhsVal, trapMessage);
    }
};

/// @brief Compute functor for overflow-checking subtraction (CRITICAL-4 optimization).
struct OverflowSubOp
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;
    const char *trapMessage;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        dispatchOverflowingBinaryDirect<&overflowSub<int16_t>,
                                        &overflowSub<int32_t>,
                                        &overflowSub<int64_t>>(
            in, fr, bb, out, lhsVal, rhsVal, trapMessage);
    }
};

/// @brief Compute functor for overflow-checking multiplication (CRITICAL-4 optimization).
struct OverflowMulOp
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;
    const char *trapMessage;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        dispatchOverflowingBinaryDirect<&overflowMul<int16_t>,
                                        &overflowMul<int32_t>,
                                        &overflowMul<int64_t>>(
            in, fr, bb, out, lhsVal, rhsVal, trapMessage);
    }
};

/// @brief Stateless bitwise AND functor for use with applyBinary.
struct BitwiseAndOp
{
    void operator()(Slot &out, const Slot &lhs, const Slot &rhs) const
    {
        out.i64 = lhs.i64 & rhs.i64;
    }
};

/// @brief Stateless bitwise OR functor for use with applyBinary.
struct BitwiseOrOp
{
    void operator()(Slot &out, const Slot &lhs, const Slot &rhs) const
    {
        out.i64 = lhs.i64 | rhs.i64;
    }
};

/// @brief Stateless bitwise XOR functor for use with applyBinary.
struct BitwiseXorOp
{
    void operator()(Slot &out, const Slot &lhs, const Slot &rhs) const
    {
        out.i64 = lhs.i64 ^ rhs.i64;
    }
};

/// @brief Stateless left-shift functor for use with applyBinary.
struct ShiftLeftOp
{
    void operator()(Slot &out, const Slot &lhs, const Slot &rhs) const
    {
        const uint64_t shift = static_cast<uint64_t>(rhs.i64) & 63U;
        const uint64_t value = static_cast<uint64_t>(lhs.i64);
        out.i64 = static_cast<int64_t>(value << shift);
    }
};

/// @brief Stateless logical right-shift functor for use with applyBinary.
struct LogicalShiftRightOp
{
    void operator()(Slot &out, const Slot &lhs, const Slot &rhs) const
    {
        const uint64_t shift = static_cast<uint64_t>(rhs.i64) & 63U;
        const uint64_t value = static_cast<uint64_t>(lhs.i64);
        out.i64 = static_cast<int64_t>(value >> shift);
    }
};

/// @brief Stateless arithmetic right-shift functor for use with applyBinary.
struct ArithmeticShiftRightOp
{
    void operator()(Slot &out, const Slot &lhs, const Slot &rhs) const
    {
        const uint64_t shift = static_cast<uint64_t>(rhs.i64) & 63U;
        if (shift == 0)
        {
            out.i64 = lhs.i64;
            return;
        }
        const uint64_t value = static_cast<uint64_t>(lhs.i64);
        const bool isNegative = (value & (uint64_t{1} << 63U)) != 0;
        uint64_t shifted = value >> shift;
        if (isNegative)
        {
            const uint64_t mask = (~uint64_t{0}) << (64U - shift);
            shifted |= mask;
        }
        out.i64 = static_cast<int64_t>(shifted);
    }
};

/// @brief Stateless unsigned division compute functor.
struct UnsignedDivOp
{
    void operator()(uint64_t dividend, uint64_t divisor, uint64_t &result) const
    {
        result = dividend / divisor;
    }
};

/// @brief Stateless unsigned remainder compute functor.
struct UnsignedRemOp
{
    void operator()(uint64_t dividend, uint64_t divisor, uint64_t &result) const
    {
        result = dividend % divisor;
    }
};

/// @brief Apply unsigned division or remainder with zero-check.
/// @tparam ComputeOp Stateless functor type for the operation.
/// @param in Instruction being executed.
/// @param fr Active frame.
/// @param bb Current basic block.
/// @param out Output slot.
/// @param lhsVal Left operand slot.
/// @param rhsVal Right operand slot.
/// @param trapMessage Message for divide-by-zero trap.
template <typename ComputeOp>
inline void applyUnsignedDivOrRemDirect(const il::core::Instr &in,
                                        Frame &fr,
                                        const il::core::BasicBlock *bb,
                                        Slot &out,
                                        const Slot &lhsVal,
                                        const Slot &rhsVal,
                                        const char *trapMessage)
{
    const auto divisor = static_cast<uint64_t>(rhsVal.i64);
    if (divisor == 0)
    {
        emitTrap(TrapKind::DivideByZero, trapMessage, in, fr, bb);
        return;
    }
    const auto dividend = static_cast<uint64_t>(lhsVal.i64);
    uint64_t result{};
    ComputeOp{}(dividend, divisor, result);
    out.i64 = static_cast<int64_t>(result);
}

/// @brief Compute functor for unsigned division with zero-check (CRITICAL-4 optimization).
struct UnsignedDivWithCheck
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;
    const char *trapMessage;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        applyUnsignedDivOrRemDirect<UnsignedDivOp>(in, fr, bb, out, lhsVal, rhsVal, trapMessage);
    }
};

/// @brief Compute functor for unsigned remainder with zero-check (CRITICAL-4 optimization).
struct UnsignedRemWithCheck
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;
    const char *trapMessage;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        applyUnsignedDivOrRemDirect<UnsignedRemOp>(in, fr, bb, out, lhsVal, rhsVal, trapMessage);
    }
};

/// @brief Compute functor for signed division with type dispatch (CRITICAL-4 optimization).
struct SignedDivWithDispatch
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        dispatchCheckedSignedBinary<&applySignedDiv<int16_t>,
                                    &applySignedDiv<int32_t>,
                                    &applySignedDiv<int64_t>>(in, fr, bb, out, lhsVal, rhsVal);
    }
};

/// @brief Compute functor for signed remainder with type dispatch (CRITICAL-4 optimization).
struct SignedRemWithDispatch
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        dispatchCheckedSignedBinary<&applySignedRem<int16_t>,
                                    &applySignedRem<int32_t>,
                                    &applySignedRem<int64_t>>(in, fr, bb, out, lhsVal, rhsVal);
    }
};

/// @brief Compute functor for checked signed division (CRITICAL-4 optimization).
struct CheckedSignedDivWithDispatch
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        dispatchCheckedSignedBinary<&applyCheckedDiv<int16_t>,
                                    &applyCheckedDiv<int32_t>,
                                    &applyCheckedDiv<int64_t>>(in, fr, bb, out, lhsVal, rhsVal);
    }
};

/// @brief Compute functor for checked signed remainder (CRITICAL-4 optimization).
struct CheckedSignedRemWithDispatch
{
    const il::core::Instr &in;
    Frame &fr;
    const il::core::BasicBlock *bb;

    void operator()(Slot &out, const Slot &lhsVal, const Slot &rhsVal) const
    {
        dispatchCheckedSignedBinary<&applyCheckedRem<int16_t>,
                                    &applyCheckedRem<int32_t>,
                                    &applyCheckedRem<int64_t>>(in, fr, bb, out, lhsVal, rhsVal);
    }
};

} // namespace il::vm::detail::integer
