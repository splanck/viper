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

template <typename NarrowT>
[[nodiscard]] bool fitsSignedRange(int64_t value)
{
    return value >= static_cast<int64_t>(std::numeric_limits<NarrowT>::min()) &&
           value <= static_cast<int64_t>(std::numeric_limits<NarrowT>::max());
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
                                switch (in.type.kind)
                                {
                                    case Type::Kind::I16:
                                        applyOverflowingBinary<int16_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in iadd.ovf",
                                            [](int16_t lhs, int16_t rhs, int16_t *res)
                                            { return __builtin_add_overflow(lhs, rhs, res); });
                                        break;
                                    case Type::Kind::I32:
                                        applyOverflowingBinary<int32_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in iadd.ovf",
                                            [](int32_t lhs, int32_t rhs, int32_t *res)
                                            { return __builtin_add_overflow(lhs, rhs, res); });
                                        break;
                                    case Type::Kind::I64:
                                    default:
                                        applyOverflowingBinary<int64_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in iadd.ovf",
                                            [](int64_t lhs, int64_t rhs, int64_t *res)
                                            { return __builtin_add_overflow(lhs, rhs, res); });
                                        break;
                                }
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
                                switch (in.type.kind)
                                {
                                    case Type::Kind::I16:
                                        applyOverflowingBinary<int16_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in isub.ovf",
                                            [](int16_t lhs, int16_t rhs, int16_t *res)
                                            { return __builtin_sub_overflow(lhs, rhs, res); });
                                        break;
                                    case Type::Kind::I32:
                                        applyOverflowingBinary<int32_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in isub.ovf",
                                            [](int32_t lhs, int32_t rhs, int32_t *res)
                                            { return __builtin_sub_overflow(lhs, rhs, res); });
                                        break;
                                    case Type::Kind::I64:
                                    default:
                                        applyOverflowingBinary<int64_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in isub.ovf",
                                            [](int64_t lhs, int64_t rhs, int64_t *res)
                                            { return __builtin_sub_overflow(lhs, rhs, res); });
                                        break;
                                }
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
                                switch (in.type.kind)
                                {
                                    case Type::Kind::I16:
                                        applyOverflowingBinary<int16_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in imul.ovf",
                                            [](int16_t lhs, int16_t rhs, int16_t *res)
                                            { return __builtin_mul_overflow(lhs, rhs, res); });
                                        break;
                                    case Type::Kind::I32:
                                        applyOverflowingBinary<int32_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in imul.ovf",
                                            [](int32_t lhs, int32_t rhs, int32_t *res)
                                            { return __builtin_mul_overflow(lhs, rhs, res); });
                                        break;
                                    case Type::Kind::I64:
                                    default:
                                        applyOverflowingBinary<int64_t>(
                                            in,
                                            fr,
                                            bb,
                                            out,
                                            lhsVal,
                                            rhsVal,
                                            "integer overflow in imul.ovf",
                                            [](int64_t lhs, int64_t rhs, int64_t *res)
                                            { return __builtin_mul_overflow(lhs, rhs, res); });
                                        break;
                                }
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
                                switch (in.type.kind)
                                {
                                    case Type::Kind::I16:
                                        applyCheckedDiv<int16_t>(in, fr, bb, out, lhsVal, rhsVal);
                                        break;
                                    case Type::Kind::I32:
                                        applyCheckedDiv<int32_t>(in, fr, bb, out, lhsVal, rhsVal);
                                        break;
                                    case Type::Kind::I64:
                                    default:
                                        applyCheckedDiv<int64_t>(in, fr, bb, out, lhsVal, rhsVal);
                                        break;
                                }
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
                                const auto divisor = static_cast<uint64_t>(rhsVal.i64);
                                if (divisor == 0)
                                {
                                    emitTrap(TrapKind::DivideByZero,
                                             "divide by zero in udiv.chk0",
                                             in,
                                             fr,
                                             bb);
                                    return;
                                }
                                const auto dividend = static_cast<uint64_t>(lhsVal.i64);
                                out.i64 = static_cast<int64_t>(dividend / divisor);
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
                                switch (in.type.kind)
                                {
                                    case Type::Kind::I16:
                                        applyCheckedRem<int16_t>(in, fr, bb, out, lhsVal, rhsVal);
                                        break;
                                    case Type::Kind::I32:
                                        applyCheckedRem<int32_t>(in, fr, bb, out, lhsVal, rhsVal);
                                        break;
                                    case Type::Kind::I64:
                                    default:
                                        applyCheckedRem<int64_t>(in, fr, bb, out, lhsVal, rhsVal);
                                        break;
                                }
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
                                const auto divisor = static_cast<uint64_t>(rhsVal.i64);
                                if (divisor == 0)
                                {
                                    emitTrap(TrapKind::DivideByZero,
                                             "divide by zero in urem.chk0",
                                             in,
                                             fr,
                                             bb);
                                    return;
                                }
                                const auto dividend = static_cast<uint64_t>(lhsVal.i64);
                                out.i64 = static_cast<int64_t>(dividend % divisor);
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
            const auto idx = static_cast<int16_t>(idxSlot.i64);
            const auto lo = static_cast<int16_t>(loSlot.i64);
            const auto hi = static_cast<int16_t>(hiSlot.i64);
            if (idx < lo || idx > hi)
            {
                trapBounds();
                return {};
            }
            out.i64 = static_cast<int64_t>(idx);
            break;
        }
        case Type::Kind::I32:
        {
            const auto idx = static_cast<int32_t>(idxSlot.i64);
            const auto lo = static_cast<int32_t>(loSlot.i64);
            const auto hi = static_cast<int32_t>(hiSlot.i64);
            if (idx < lo || idx > hi)
            {
                trapBounds();
                return {};
            }
            out.i64 = static_cast<int64_t>(idx);
            break;
        }
        default:
        {
            const auto idx = idxSlot.i64;
            const auto lo = loSlot.i64;
            const auto hi = hiSlot.i64;
            if (idx < lo || idx > hi)
            {
                trapBounds();
                return {};
            }
            out.i64 = idx;
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

/// @brief Interpret the `trunc1`/`zext1` opcodes that normalise between `i1` and `i64`.
/// @note The operand is masked to the least-significant bit so the stored value is a
///       canonical boolean per docs/il-guide.md#reference §Conversions.
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
    Slot value = vm.eval(fr, in.operands[0]);
    value.i64 &= 1;
    ops::storeResult(fr, in, value);
    return {};
}

} // namespace il::vm::detail

