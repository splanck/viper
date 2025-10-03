// MIT License. See LICENSE in the project root for full license information.
// File: src/vm/int_ops.cpp
// Purpose: Implement VM handlers for integer arithmetic, bitwise logic, comparisons, and
//          1-bit conversions.
// Key invariants: Results use 64-bit two's complement semantics consistent with the IL
//                 reference, and handlers only mutate the current frame.
// Links: docs/il-guide.md#reference §Integer Arithmetic, §Bitwise and Shifts, §Comparisons,
//        §Conversions

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"

#include <cstdint>
#include <limits>
#include <utility>

using namespace il::core;

namespace il::vm::detail
{
namespace
{
void emitTrap(TrapKind kind,
              const char *message,
              const Instr &in,
              Frame &fr,
              const BasicBlock *bb)
{
    RuntimeBridge::trap(kind, message, in.loc, fr.func->name, bb ? bb->label : "");
}

template <typename T, typename OverflowOp>
void applyOverflowingBinary(const Instr &in,
                            Frame &fr,
                            const BasicBlock *bb,
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
void dispatchOverflowingBinary(const Instr &in,
                               Frame &fr,
                               const BasicBlock *bb,
                               Slot &out,
                               const Slot &lhsVal,
                               const Slot &rhsVal,
                               const char *trapMessage,
                               OverflowOp overflowOp)
{
    switch (in.type.kind)
    {
        case Type::Kind::I16:
            applyOverflowingBinary<int16_t>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage, overflowOp);
            break;
        case Type::Kind::I32:
            applyOverflowingBinary<int32_t>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage, overflowOp);
            break;
        case Type::Kind::I64:
        default:
            applyOverflowingBinary<int64_t>(
                in, fr, bb, out, lhsVal, rhsVal, trapMessage, overflowOp);
            break;
    }
}

template <typename T>
void applySignedDiv(const Instr &in,
                    Frame &fr,
                    const BasicBlock *bb,
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
void applySignedRem(const Instr &in,
                    Frame &fr,
                    const BasicBlock *bb,
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
void applyCheckedDiv(const Instr &in,
                     Frame &fr,
                     const BasicBlock *bb,
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
void applyCheckedRem(const Instr &in,
                     Frame &fr,
                     const BasicBlock *bb,
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

using CheckedSignedBinaryFn = void (*)(const Instr &,
                                       Frame &,
                                       const BasicBlock *,
                                       Slot &,
                                       const Slot &,
                                       const Slot &);

template <CheckedSignedBinaryFn ApplyI16,
          CheckedSignedBinaryFn ApplyI32,
          CheckedSignedBinaryFn ApplyI64>
void dispatchCheckedSignedBinary(const Instr &in,
                                 Frame &fr,
                                 const BasicBlock *bb,
                                 Slot &out,
                                 const Slot &lhsVal,
                                 const Slot &rhsVal)
{
    switch (in.type.kind)
    {
        case Type::Kind::I16:
            ApplyI16(in, fr, bb, out, lhsVal, rhsVal);
            break;
        case Type::Kind::I32:
            ApplyI32(in, fr, bb, out, lhsVal, rhsVal);
            break;
        case Type::Kind::I64:
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
    if (idx < lo || idx > hi)
    {
        return {false, static_cast<int64_t>(idx)};
    }
    return {true, static_cast<int64_t>(idx)};
}

template <typename NarrowT>
[[nodiscard]] bool fitsSignedRange(int64_t value)
{
    return value >= static_cast<int64_t>(std::numeric_limits<NarrowT>::min()) &&
           value <= static_cast<int64_t>(std::numeric_limits<NarrowT>::max());
}

template <typename ComputeOp>
void applyUnsignedDivOrRem(const Instr &in,
                           Frame &fr,
                           const BasicBlock *bb,
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

template <typename NarrowT>
[[nodiscard]] bool fitsUnsignedRange(uint64_t value)
{
    return value <= static_cast<uint64_t>(std::numeric_limits<NarrowT>::max());
}

struct SignedNarrowCastTraits
{
    using WideType = int64_t;
    static constexpr const char *kOutOfRangeMessage =
        "value out of range in cast.si_narrow.chk";
    static constexpr const char *kUnsupportedTypeMessage =
        "unsupported target type in cast.si_narrow.chk";

    static WideType toWide(int64_t raw) { return raw; }

    static int64_t toStorage(WideType value) { return value; }

    template <typename NarrowT>
    static bool fits(WideType value)
    {
        return fitsSignedRange<NarrowT>(value);
    }

    template <typename NarrowT>
    static int64_t narrow(WideType value)
    {
        return static_cast<int64_t>(static_cast<NarrowT>(value));
    }

    static bool checkBoolean(WideType value)
    {
        return (value == 0) || (value == 1);
    }

    static int64_t booleanValue(WideType value)
    {
        return value & 1;
    }
};

struct UnsignedNarrowCastTraits
{
    using WideType = uint64_t;
    static constexpr const char *kOutOfRangeMessage =
        "value out of range in cast.ui_narrow.chk";
    static constexpr const char *kUnsupportedTypeMessage =
        "unsupported target type in cast.ui_narrow.chk";

    static WideType toWide(int64_t raw)
    {
        return static_cast<WideType>(raw);
    }

    static int64_t toStorage(WideType value)
    {
        return static_cast<int64_t>(value);
    }

    template <typename NarrowT>
    static bool fits(WideType value)
    {
        return fitsUnsignedRange<NarrowT>(value);
    }

    template <typename NarrowT>
    static int64_t narrow(WideType value)
    {
        return static_cast<int64_t>(static_cast<NarrowT>(value));
    }

    static bool checkBoolean(WideType value)
    {
        return value <= 1;
    }

    static int64_t booleanValue(WideType value)
    {
        return static_cast<int64_t>(value & 1);
    }
};

template <typename Traits>
VM::ExecResult handleCastNarrowChkImpl(const Slot &value,
                                       Frame &fr,
                                       const Instr &in,
                                       const BasicBlock *bb)
{
    using Wide = typename Traits::WideType;
    const Wide operand = Traits::toWide(value.i64);

    auto trapOutOfRange = [&]() {
        emitTrap(TrapKind::InvalidCast, Traits::kOutOfRangeMessage, in, fr, bb);
    };

    int64_t narrowed = Traits::toStorage(operand);
    bool inRange = true;
    switch (in.type.kind)
    {
        case Type::Kind::I16:
            inRange = Traits::template fits<int16_t>(operand);
            if (inRange)
            {
                narrowed = Traits::template narrow<int16_t>(operand);
            }
            break;
        case Type::Kind::I32:
            inRange = Traits::template fits<int32_t>(operand);
            if (inRange)
            {
                narrowed = Traits::template narrow<int32_t>(operand);
            }
            break;
        case Type::Kind::I1:
            inRange = Traits::checkBoolean(operand);
            if (inRange)
            {
                narrowed = Traits::booleanValue(operand);
            }
            break;
        case Type::Kind::I64:
            break;
        default:
            emitTrap(TrapKind::InvalidCast,
                     Traits::kUnsupportedTypeMessage,
                     in,
                     fr,
                     bb);
            return {};
    }

    if (!inRange)
    {
        trapOutOfRange();
        return {};
    }

    Slot out{};
    out.i64 = narrowed;
    ops::storeResult(fr, in, out);
    return {};
}
} // namespace

/// @brief Interpret the `add` opcode for 64-bit integers.
/// @param vm Active VM used to evaluate operand values.
/// @param fr Execution frame mutated to hold the result.
/// @param in Instruction carrying operand descriptors and destination register.
/// @param blocks Unused lookup table for this opcode (required by signature).
/// @param bb Unused basic block pointer for this opcode.
/// @param ip Unused instruction index for this opcode.
/// @return Normal execution result without control transfer.
/// @note Operands are summed as signed 64-bit values with two's complement
///       wrap-around, matching docs/il-guide.md#reference §Integer Arithmetic and the
///       `i64` type rules in §Types.
VM::ExecResult OpHandlers::handleAdd(VM &vm,
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
                            { out.i64 = lhsVal.i64 + rhsVal.i64; });
}

/// @brief Interpret the `sub` opcode for 64-bit integers.
/// @note Operand evaluation and frame updates mirror @ref OpHandlers::handleAdd, with subtraction
///       obeying two's complement wrap semantics per docs/il-guide.md#reference §Integer Arithmetic.
VM::ExecResult OpHandlers::handleSub(VM &vm,
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
                            { out.i64 = lhsVal.i64 - rhsVal.i64; });
}

/// @brief Interpret the `isub` opcode for 64-bit integers.
/// @note Routes to @ref OpHandlers::handleSub to share two's complement semantics with
///       the general subtraction handler.
VM::ExecResult OpHandlers::handleISub(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    return handleSub(vm, fr, in, blocks, bb, ip);
}

/// @brief Interpret the `mul` opcode for 64-bit integers.
/// @note Multiplication uses the same operand handling helpers as addition, wraps
///       modulo 2^64 per docs/il-guide.md#reference §Integer Arithmetic, and stores the
///       result back into the destination register.
VM::ExecResult OpHandlers::handleMul(VM &vm,
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
                            { out.i64 = lhsVal.i64 * rhsVal.i64; });
}

/// @brief Interpret the `iadd.ovf` opcode, trapping on signed overflow.
VM::ExecResult OpHandlers::handleIAddOvf(VM &vm,
                                         Frame &fr,
                                         const Instr &in,
                                         const VM::BlockMap &blocks,
                                         const BasicBlock *&bb,
                                         size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                dispatchOverflowingBinary(
                                    in,
                                    fr,
                                    bb,
                                    out,
                                    lhsVal,
                                    rhsVal,
                                    "integer overflow in iadd.ovf",
                                    [](auto lhs, auto rhs, auto *res)
                                    { return __builtin_add_overflow(lhs, rhs, res); });
                            });
}

/// @brief Interpret the `isub.ovf` opcode, trapping on signed overflow.
VM::ExecResult OpHandlers::handleISubOvf(VM &vm,
                                         Frame &fr,
                                         const Instr &in,
                                         const VM::BlockMap &blocks,
                                         const BasicBlock *&bb,
                                         size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                dispatchOverflowingBinary(
                                    in,
                                    fr,
                                    bb,
                                    out,
                                    lhsVal,
                                    rhsVal,
                                    "integer overflow in isub.ovf",
                                    [](auto lhs, auto rhs, auto *res)
                                    { return __builtin_sub_overflow(lhs, rhs, res); });
                            });
}

/// @brief Interpret the `imul.ovf` opcode, trapping on signed overflow.
VM::ExecResult OpHandlers::handleIMulOvf(VM &vm,
                                         Frame &fr,
                                         const Instr &in,
                                         const VM::BlockMap &blocks,
                                         const BasicBlock *&bb,
                                         size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                dispatchOverflowingBinary(
                                    in,
                                    fr,
                                    bb,
                                    out,
                                    lhsVal,
                                    rhsVal,
                                    "integer overflow in imul.ovf",
                                    [](auto lhs, auto rhs, auto *res)
                                    { return __builtin_mul_overflow(lhs, rhs, res); });
                            });
}

/// @brief Interpret the `sdiv` opcode with divide-by-zero and overflow trapping.
VM::ExecResult OpHandlers::handleSDiv(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                dispatchCheckedSignedBinary<&applySignedDiv<int16_t>,
                                                             &applySignedDiv<int32_t>,
                                                             &applySignedDiv<int64_t>>(
                                    in, fr, bb, out, lhsVal, rhsVal);
                            });
}

/// @brief Interpret the `udiv` opcode with divide-by-zero trapping.
VM::ExecResult OpHandlers::handleUDiv(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                applyUnsignedDivOrRem(
                                    in,
                                    fr,
                                    bb,
                                    out,
                                    lhsVal,
                                    rhsVal,
                                    "divide by zero in udiv",
                                    [](uint64_t dividend, uint64_t divisor)
                                    { return dividend / divisor; });
                            });
}

/// @brief Interpret the `srem` opcode with divide-by-zero trapping.
VM::ExecResult OpHandlers::handleSRem(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                dispatchCheckedSignedBinary<&applySignedRem<int16_t>,
                                                             &applySignedRem<int32_t>,
                                                             &applySignedRem<int64_t>>(
                                    in, fr, bb, out, lhsVal, rhsVal);
                            });
}

/// @brief Interpret the `urem` opcode with divide-by-zero trapping.
VM::ExecResult OpHandlers::handleURem(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                applyUnsignedDivOrRem(
                                    in,
                                    fr,
                                    bb,
                                    out,
                                    lhsVal,
                                    rhsVal,
                                    "divide by zero in urem",
                                    [](uint64_t dividend, uint64_t divisor)
                                    { return dividend % divisor; });
                            });
}

/// @brief Interpret the `sdiv.chk0` opcode with divide-by-zero and overflow trapping.
VM::ExecResult OpHandlers::handleSDivChk0(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                dispatchCheckedSignedBinary<&applyCheckedDiv<int16_t>,
                                                             &applyCheckedDiv<int32_t>,
                                                             &applyCheckedDiv<int64_t>>(
                                    in, fr, bb, out, lhsVal, rhsVal);
                            });
}

/// @brief Interpret the `udiv.chk0` opcode with divide-by-zero trapping.
VM::ExecResult OpHandlers::handleUDivChk0(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                applyUnsignedDivOrRem(
                                    in,
                                    fr,
                                    bb,
                                    out,
                                    lhsVal,
                                    rhsVal,
                                    "divide by zero in udiv.chk0",
                                    [](uint64_t dividend, uint64_t divisor)
                                    { return dividend / divisor; });
                            });
}

/// @brief Interpret the `srem.chk0` opcode with divide-by-zero and overflow trapping.
VM::ExecResult OpHandlers::handleSRemChk0(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                dispatchCheckedSignedBinary<&applyCheckedRem<int16_t>,
                                                             &applyCheckedRem<int32_t>,
                                                             &applyCheckedRem<int64_t>>(
                                    in, fr, bb, out, lhsVal, rhsVal);
                            });
}

/// @brief Interpret the `urem.chk0` opcode with divide-by-zero trapping.
VM::ExecResult OpHandlers::handleURemChk0(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                applyUnsignedDivOrRem(
                                    in,
                                    fr,
                                    bb,
                                    out,
                                    lhsVal,
                                    rhsVal,
                                    "divide by zero in urem.chk0",
                                    [](uint64_t dividend, uint64_t divisor)
                                    { return dividend % divisor; });
                            });
}

/// @brief Interpret the `idx.chk` opcode, trapping when the index leaves the inclusive range.
VM::ExecResult OpHandlers::handleIdxChk(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    const Slot idxSlot = vm.eval(fr, in.operands[0]);
    const Slot loSlot = vm.eval(fr, in.operands[1]);
    const Slot hiSlot = vm.eval(fr, in.operands[2]);

    auto trapBounds = []()
    {
        vm_raise(TrapKind::Bounds);
    };

    Slot out{};
    switch (in.type.kind)
    {
        case Type::Kind::I16:
        {
            const auto [inBounds, normalized] =
                performBoundsCheck<int16_t>(idxSlot, loSlot, hiSlot);
            if (!inBounds)
            {
                trapBounds();
                return {};
            }
            out.i64 = normalized;
            break;
        }
        case Type::Kind::I32:
        {
            const auto [inBounds, normalized] =
                performBoundsCheck<int32_t>(idxSlot, loSlot, hiSlot);
            if (!inBounds)
            {
                trapBounds();
                return {};
            }
            out.i64 = normalized;
            break;
        }
        default:
        {
            const auto [inBounds, normalized] =
                performBoundsCheck<int64_t>(idxSlot, loSlot, hiSlot);
            if (!inBounds)
            {
                trapBounds();
                return {};
            }
            out.i64 = normalized;
            break;
        }
    }

    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Interpret the `cast.si_narrow.chk` opcode with range checking for signed integers.
VM::ExecResult OpHandlers::handleCastSiNarrowChk(VM &vm,
                                                 Frame &fr,
                                                 const Instr &in,
                                                 const VM::BlockMap &blocks,
                                                 const BasicBlock *&bb,
                                                 size_t &ip)
{
    (void)blocks;
    (void)ip;
    const Slot value = vm.eval(fr, in.operands[0]);
    return handleCastNarrowChkImpl<SignedNarrowCastTraits>(value, fr, in, bb);
}

/// @brief Interpret the `cast.ui_narrow.chk` opcode with range checking for unsigned integers.
VM::ExecResult OpHandlers::handleCastUiNarrowChk(VM &vm,
                                                 Frame &fr,
                                                 const Instr &in,
                                                 const VM::BlockMap &blocks,
                                                 const BasicBlock *&bb,
                                                 size_t &ip)
{
    (void)blocks;
    (void)ip;
    const Slot value = vm.eval(fr, in.operands[0]);
    return handleCastNarrowChkImpl<UnsignedNarrowCastTraits>(value, fr, in, bb);
}

/// @brief Interpret the `cast.si_to_fp` opcode converting signed integers to double.
VM::ExecResult OpHandlers::handleCastSiToFp(VM &vm,
                                            Frame &fr,
                                            const Instr &in,
                                            const VM::BlockMap &blocks,
                                            const BasicBlock *&bb,
                                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    const Slot value = vm.eval(fr, in.operands[0]);
    Slot out{};
    out.f64 = static_cast<double>(value.i64);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Interpret the `cast.ui_to_fp` opcode converting unsigned integers to double.
VM::ExecResult OpHandlers::handleCastUiToFp(VM &vm,
                                            Frame &fr,
                                            const Instr &in,
                                            const VM::BlockMap &blocks,
                                            const BasicBlock *&bb,
                                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    const Slot value = vm.eval(fr, in.operands[0]);
    const uint64_t operand = static_cast<uint64_t>(value.i64);
    Slot out{};
    out.f64 = static_cast<double>(operand);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Interpret the `and` opcode for 64-bit integers.
/// @note Applies bitwise conjunction on canonical 64-bit operands and writes the
///       result to the destination register, as specified in docs/il-guide.md#reference.
VM::ExecResult OpHandlers::handleAnd(VM &vm,
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
                            { out.i64 = lhsVal.i64 & rhsVal.i64; });
}

/// @brief Interpret the `or` opcode for 64-bit integers.
/// @note Applies bitwise disjunction on canonical 64-bit operands and writes the
///       result to the destination register, following docs/il-guide.md#reference.
VM::ExecResult OpHandlers::handleOr(VM &vm,
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
                            { out.i64 = lhsVal.i64 | rhsVal.i64; });
}

/// @brief Interpret the `xor` opcode for 64-bit integers.
/// @note Operands are evaluated via @c vm.eval and the bitwise result is stored back
///       into the destination register, matching docs/il-guide.md#reference §Bitwise and Shifts.
VM::ExecResult OpHandlers::handleXor(VM &vm,
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
                            { out.i64 = lhsVal.i64 ^ rhsVal.i64; });
}

/// @brief Interpret the `shl` opcode for integer left shifts.
/// @note The shift count is taken from the second operand; well-formed IL keeps it within
///       [0, 63] so the host operation remains defined, and the result is written back
///       to the frame (docs/il-guide.md#reference §Bitwise and Shifts).
VM::ExecResult OpHandlers::handleShl(VM &vm,
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
                            { out.i64 = lhsVal.i64 << rhsVal.i64; });
}

/// @brief Interpret the `lshr` opcode for logical right shifts.
/// @note Shift counts are masked to the canonical 0-63 range and the operation
///       zero-extends the vacated bits, per docs/il-guide.md#reference
///       §Bitwise and Shifts.
VM::ExecResult OpHandlers::handleLShr(VM &vm,
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
                            {
                                const uint64_t shift = static_cast<uint64_t>(rhsVal.i64) & 63U;
                                const uint64_t value = static_cast<uint64_t>(lhsVal.i64);
                                out.i64 = static_cast<int64_t>(value >> shift);
                            });
}

/// @brief Interpret the `ashr` opcode for arithmetic right shifts.
/// @note Shift counts are masked to the canonical 0-63 range and sign bits are
///       preserved for negative operands, matching docs/il-guide.md#reference
///       §Bitwise and Shifts.
VM::ExecResult OpHandlers::handleAShr(VM &vm,
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
                            {
                                const uint64_t shift = static_cast<uint64_t>(rhsVal.i64) & 63U;
                                if (shift == 0)
                                {
                                    out.i64 = lhsVal.i64;
                                    return;
                                }

                                const uint64_t value = static_cast<uint64_t>(lhsVal.i64);
                                const bool isNegative = (value & (uint64_t{1} << 63U)) != 0;
                                uint64_t shifted = value >> shift;
                                if (isNegative)
                                {
                                    const uint64_t mask = (~uint64_t{0}) << (64U - shift);
                                    shifted |= mask;
                                }
                                out.i64 = static_cast<int64_t>(shifted);
                            });
}

/// @brief Interpret the `icmp_eq` opcode for integer equality comparisons.
/// @note Produces a canonical `i1` value (0 or 1) stored via @c ops::storeResult,
///       following docs/il-guide.md#reference §Comparisons.
VM::ExecResult OpHandlers::handleICmpEq(VM &vm,
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
                             { return lhsVal.i64 == rhsVal.i64; });
}

/// @brief Interpret the `icmp_ne` opcode for integer inequality comparisons.
/// @note Semantics mirror @ref OpHandlers::handleICmpEq with negated predicate per docs/il-guide.md#reference §Comparisons.
VM::ExecResult OpHandlers::handleICmpNe(VM &vm,
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
                             { return lhsVal.i64 != rhsVal.i64; });
}

/// @brief Interpret the `scmp_gt` opcode for signed greater-than comparisons.
/// @note Reads both operands as signed 64-bit integers and stores a canonical `i1`
///       result, consistent with docs/il-guide.md#reference §Comparisons.
VM::ExecResult OpHandlers::handleSCmpGT(VM &vm,
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
                             { return lhsVal.i64 > rhsVal.i64; });
}

/// @brief Interpret the `scmp_lt` opcode for signed less-than comparisons.
/// @note Shares operand evaluation and storage behaviour with other comparison handlers,
///       producing canonical booleans per docs/il-guide.md#reference §Comparisons.
VM::ExecResult OpHandlers::handleSCmpLT(VM &vm,
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
                             { return lhsVal.i64 < rhsVal.i64; });
}

/// @brief Interpret the `scmp_le` opcode for signed less-or-equal comparisons.
/// @note Uses signed ordering per docs/il-guide.md#reference §Comparisons and returns a
///       canonical `i1` result written into the destination register.
VM::ExecResult OpHandlers::handleSCmpLE(VM &vm,
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
                             { return lhsVal.i64 <= rhsVal.i64; });
}

/// @brief Interpret the `scmp_ge` opcode for signed greater-or-equal comparisons.
/// @note Completes the signed comparison set defined in docs/il-guide.md#reference §Comparisons
///       by writing 0 or 1 into the destination register.
VM::ExecResult OpHandlers::handleSCmpGE(VM &vm,
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
                             { return lhsVal.i64 >= rhsVal.i64; });
}

/// @brief Interpret the `ucmp_lt` opcode for unsigned less-than comparisons.
/// @note Operands are reinterpreted as unsigned 64-bit values per
///       docs/il-guide.md#reference §Comparisons, and the result is stored as a
///       canonical boolean.
VM::ExecResult OpHandlers::handleUCmpLT(VM &vm,
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
                             {
                                 return static_cast<uint64_t>(lhsVal.i64) <
                                        static_cast<uint64_t>(rhsVal.i64);
                             });
}

/// @brief Interpret the `ucmp_le` opcode for unsigned less-or-equal comparisons.
/// @note Reuses unsigned ordering semantics and returns 0 or 1 in the
///       destination register.
VM::ExecResult OpHandlers::handleUCmpLE(VM &vm,
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
                             {
                                 return static_cast<uint64_t>(lhsVal.i64) <=
                                        static_cast<uint64_t>(rhsVal.i64);
                             });
}

/// @brief Interpret the `ucmp_gt` opcode for unsigned greater-than comparisons.
/// @note Mirrors @ref OpHandlers::handleUCmpLT with inverted predicate while
///       maintaining canonical boolean storage.
VM::ExecResult OpHandlers::handleUCmpGT(VM &vm,
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
                             {
                                 return static_cast<uint64_t>(lhsVal.i64) >
                                        static_cast<uint64_t>(rhsVal.i64);
                             });
}

/// @brief Interpret the `ucmp_ge` opcode for unsigned greater-or-equal comparisons.
/// @note Completes the unsigned comparison family with canonical boolean
///       results.
VM::ExecResult OpHandlers::handleUCmpGE(VM &vm,
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
                             {
                                 return static_cast<uint64_t>(lhsVal.i64) >=
                                        static_cast<uint64_t>(rhsVal.i64);
                             });
}

/// @brief Interpret the `trunc1`/`zext1` opcodes that normalise between `i1` and `i64`.
/// @note Non-zero inputs are canonicalised to `1` while zero becomes `0`, matching
///       docs/il-guide.md#reference §Conversions for both truncation and extension.
VM::ExecResult OpHandlers::handleTruncOrZext1(VM &vm,
                                              Frame &fr,
                                              const Instr &in,
                                              const VM::BlockMap &blocks,
                                              const BasicBlock *&bb,
                                              size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot operand = vm.eval(fr, in.operands[0]);
    const bool truthy = operand.i64 != 0;

    Slot result{};
    switch (in.op)
    {
        case Opcode::Trunc1:
        case Opcode::Zext1:
            result.i64 = truthy ? 1 : 0;
            break;
        default:
            result = operand;
            break;
    }

    ops::storeResult(fr, in, result);
    return {};
}

} // namespace il::vm::detail

