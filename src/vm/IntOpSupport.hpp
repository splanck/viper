// MIT License. See LICENSE in the project root for full license information.
// File: src/vm/IntOpSupport.hpp
// Purpose: Shared helpers for integer opcode handlers, covering trap dispatch and
//          type-specialised arithmetic templates.
// Key invariants: Helpers operate on canonicalised Slot values and honour IL trap
//                 semantics.
// Links: docs/il-guide.md#reference §Integer Arithmetic, §Bitwise and Shifts,
//        §Comparisons, §Conversions
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

} // namespace il::vm::detail::integer
