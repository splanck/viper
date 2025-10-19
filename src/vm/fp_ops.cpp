// File: src/vm/fp_ops.cpp
// Purpose: Implement VM handlers for floating-point math and conversions.
// License: MIT License (see LICENSE).
// Key invariants: Floating-point operations follow IEEE-754 semantics of host double type.
// Ownership/Lifetime: Handlers operate on frame-local slots without retaining references.
// Assumptions: Host doubles implement IEEE-754 binary64 semantics and frames mutate only via ops::storeResult.
// Links: docs/il-guide.md#reference

#include "vm/OpHandlers_Float.hpp"

#include "il/core/Function.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

using namespace il::core;

namespace il::vm::detail::floating
{
namespace
{
constexpr double kUint64Boundary = 18446744073709551616.0; ///< 2^64, sentinel for overflow.

[[nodiscard]] uint64_t castFpToUiRoundedOrTrap(double operand,
                                               const Instr &in,
                                               Frame &fr,
                                               const BasicBlock *bb)
{
    constexpr const char *kInvalidOperandMessage =
        "invalid fp operand in cast.fp_to_ui.rte.chk";
    constexpr const char *kOverflowMessage =
        "fp overflow in cast.fp_to_ui.rte.chk";

    auto trap = [&](const char *message)
    {
        RuntimeBridge::trap(TrapKind::Overflow,
                            message,
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
    };

    if (!std::isfinite(operand) || std::signbit(operand))
    {
        trap(kInvalidOperandMessage);
    }

    if (operand >= kUint64Boundary)
    {
        trap(kOverflowMessage);
    }

    double integral = 0.0;
    const double fractional = std::modf(operand, &integral);

    if (fractional > 0.5)
    {
        integral += 1.0;
    }
    else if (fractional == 0.5)
    {
        if (std::fmod(integral, 2.0) != 0.0)
        {
            integral += 1.0;
        }
    }

    if (integral >= kUint64Boundary)
    {
        trap(kOverflowMessage);
    }

    return static_cast<uint64_t>(integral);
}
} // namespace

/// @brief Add two floating-point values and store the IEEE-754 sum.
/// @details Relies on host binary64 addition so NaNs propagate and infinities behave per IEEE-754.
/// The handler mutates the frame only by writing the result slot via ops::storeResult.
VM::ExecResult handleFAdd(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            { out.f64 = lhsVal.f64 + rhsVal.f64; });
}

/// @brief Subtract two floating-point values and store the IEEE-754 difference.
/// @details Host subtraction governs NaN propagation and signed zero handling; the frame mutation
/// is restricted to storing the result slot.
VM::ExecResult handleFSub(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            { out.f64 = lhsVal.f64 - rhsVal.f64; });
}

/// @brief Multiply two floating-point values and store the IEEE-754 product.
/// @details NaNs and infinities follow host multiplication rules, and the frame is only modified
/// through ops::storeResult.
VM::ExecResult handleFMul(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            { out.f64 = lhsVal.f64 * rhsVal.f64; });
}

/// @brief Divide two floating-point values and store the IEEE-754 quotient.
/// @details Host division semantics provide handling for NaNs, infinities, and division by zero,
/// and the only frame mutation is storing the destination slot.
VM::ExecResult handleFDiv(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            { out.f64 = lhsVal.f64 / rhsVal.f64; });
}

/// @brief Compare two floating-point values for equality and store 1 when they are equal.
/// @details Follows host IEEE-754 equality: any NaN operand yields false (0), while signed zeros
/// compare equal. Only the destination slot is written in the frame.
VM::ExecResult handleFCmpEQ(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.f64 == rhsVal.f64; });
}

/// @brief Compare two floating-point values for inequality and store 1 when they differ.
/// @details Uses host IEEE-754 semantics where NaN operands cause the predicate to succeed,
/// yielding 1; otherwise, equality produces 0. The frame mutation is limited to the result slot.
VM::ExecResult handleFCmpNE(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.f64 != rhsVal.f64; });
}

/// @brief Compare two floating-point values and store 1 when lhs > rhs under IEEE-754 ordering.
/// @details If either operand is NaN the predicate is false and 0 is stored; only the destination
/// slot in the frame is mutated.
VM::ExecResult handleFCmpGT(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.f64 > rhsVal.f64; });
}

/// @brief Compare two floating-point values and store 1 when lhs < rhs under IEEE-754 ordering.
/// @details NaN operands force the predicate to false (0). Frame mutation is restricted to the
/// destination slot.
VM::ExecResult handleFCmpLT(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.f64 < rhsVal.f64; });
}

/// @brief Compare two floating-point values and store 1 when lhs <= rhs.
/// @details Host IEEE-754 semantics mean NaN operands yield 0, while signed zeros compare as equal;
/// only the result slot is modified in the frame.
VM::ExecResult handleFCmpLE(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.f64 <= rhsVal.f64; });
}

/// @brief Compare two floating-point values and store 1 when lhs >= rhs.
/// @details NaN operands make the predicate false (0). The handler touches the frame solely via
/// ops::storeResult.
VM::ExecResult handleFCmpGE(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(vm,
                             fr,
                             in,
                             [](const Slot &lhsVal, const Slot &rhsVal)
                             { return lhsVal.f64 >= rhsVal.f64; });
}

/// @brief Convert a signed 64-bit integer to an IEEE-754 binary64 value.
/// @details Relies on host conversion semantics; large magnitudes round according to IEEE-754 and
/// the frame is mutated only when storing the result slot.
VM::ExecResult handleSitofp(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    Slot out{};
    out.f64 = static_cast<double>(value.i64);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Convert a floating-point value to a signed 64-bit integer using truncation toward zero.
/// @details Assumes the source is a finite value representable in int64_t; NaNs or out-of-range
/// values exhibit host-defined (potentially undefined) behaviour. The handler mutates the frame
/// solely by storing the result slot.
VM::ExecResult handleFptosi(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)ip;
    const Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    const double operand = value.f64;
    if (!std::isfinite(operand))
    {
        RuntimeBridge::trap(TrapKind::InvalidCast,
                            "invalid fp operand in fptosi",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
        return {};
    }

    constexpr double kLowerBound = -std::ldexp(1.0, 63);
    constexpr double kUpperBound = std::ldexp(1.0, 63);
    if (operand < kLowerBound || operand >= kUpperBound)
    {
        RuntimeBridge::trap(TrapKind::Overflow,
                            "fp overflow in fptosi",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
        return {};
    }

    const double truncated = std::trunc(operand);
    if (truncated < kLowerBound || truncated >= kUpperBound)
    {
        RuntimeBridge::trap(TrapKind::Overflow,
                            "fp overflow in fptosi",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
        return {};
    }

    Slot out{};
    out.i64 = static_cast<int64_t>(truncated);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Convert a floating-point value to a signed 64-bit integer using round-to-nearest-even.
/// @details Traps via RuntimeBridge when the operand is non-finite or the rounded result lies
/// outside the signed 64-bit range. Rounding obeys IEEE-754 round-to-nearest, ties-to-even via
/// std::nearbyint operating under the default FE_TONEAREST mode.
VM::ExecResult handleCastFpToSiRteChk(VM &vm,
                                                  Frame &fr,
                                                  const Instr &in,
                                                  const VM::BlockMap &blocks,
                                                  const BasicBlock *&bb,
                                                  size_t &ip)
{
    (void)blocks;
    (void)ip;
    const Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    const double operand = value.f64;
    if (!std::isfinite(operand))
    {
        RuntimeBridge::trap(TrapKind::InvalidCast,
                            "invalid fp operand in cast.fp_to_si.rte.chk",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
    }

    const double rounded = std::nearbyint(operand);
    if (!std::isfinite(rounded))
    {
        RuntimeBridge::trap(TrapKind::Overflow,
                            "fp overflow in cast.fp_to_si.rte.chk",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
    }

    constexpr double kMin = static_cast<double>(std::numeric_limits<int64_t>::min());
    constexpr double kMax = static_cast<double>(std::numeric_limits<int64_t>::max());
    if (rounded < kMin || rounded > kMax)
    {
        RuntimeBridge::trap(TrapKind::Overflow,
                            "fp overflow in cast.fp_to_si.rte.chk",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
    }

    Slot out{};
    out.i64 = static_cast<int64_t>(rounded);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Convert a floating-point value to an unsigned 64-bit integer using round-to-nearest-even.
/// @details Rejects NaNs, negative inputs (including negative zero), and values whose rounded result
/// falls outside the unsigned 64-bit range by trapping with TrapKind::Overflow. Rounding uses the
/// banker’s rule (ties to even) implemented via castFpToUiRoundedOrTrap to maintain deterministic
/// behaviour across platforms.
VM::ExecResult handleCastFpToUiRteChk(VM &vm,
                                                  Frame &fr,
                                                  const Instr &in,
                                                  const VM::BlockMap &blocks,
                                                  const BasicBlock *&bb,
                                                  size_t &ip)
{
    (void)blocks;
    (void)ip;
    const Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    const uint64_t rounded = castFpToUiRoundedOrTrap(value.f64, in, fr, bb);

    Slot out{};
    out.i64 = static_cast<int64_t>(rounded);
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail::floating

