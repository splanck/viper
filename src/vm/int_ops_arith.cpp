//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements integer arithmetic, division, and bitwise opcode handlers for the
// VM. Each helper orchestrates register reads, invokes the shared math helpers,
// and writes back results while respecting the trap semantics defined in the
// IL specification.
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Int.hpp"

#include "vm/IntOpSupport.hpp"

namespace il::vm::detail::integer
{
/// @brief Dispatch the generic subtraction helper for the `isub` opcode.
///
/// The instruction shares the same semantics as the standard integer
/// subtraction handler, so this wrapper simply forwards to @ref handleSub while
/// preserving the expected signature for the opcode dispatch table.
VM::ExecResult handleISub(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip)
{
    return handleSub(vm, fr, in, blocks, bb, ip);
}

/// @brief Execute `iadd.ovf`, trapping on signed overflow.
///
/// Evaluates both operands via @ref ops::applyBinary and routes the arithmetic
/// through @ref dispatchOverflowingBinary so the helper can raise structured
/// traps when the checked addition exceeds the destination width.
VM::ExecResult handleIAddOvf(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
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
                                    { return ops::checked_add(lhs, rhs, res); });
                            });
}

/// @brief Execute `isub.ovf`, trapping on signed overflow.
///
/// Mirrors @ref handleIAddOvf but uses @ref ops::checked_sub to detect
/// overflow during subtraction, emitting the canonical diagnostic message when
/// the checked operation fails.
VM::ExecResult handleISubOvf(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
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
                                    { return ops::checked_sub(lhs, rhs, res); });
                            });
}

/// @brief Execute `imul.ovf`, trapping when the product exceeds the lane width.
///
/// Invokes @ref dispatchOverflowingBinary with @ref ops::checked_mul so signed
/// multiplication overflow surfaces as a trap rather than silently wrapping.
VM::ExecResult handleIMulOvf(VM &vm,
                             Frame &fr,
                             const il::core::Instr &in,
                             const VM::BlockMap &blocks,
                             const il::core::BasicBlock *&bb,
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
                                    { return ops::checked_mul(lhs, rhs, res); });
                            });
}

/// @brief Execute the signed `idiv` opcode without zero checking.
///
/// Uses @ref dispatchCheckedSignedBinary to select the correct integer width
/// implementation while allowing divide-by-zero traps to be deferred to the
/// specialised checked variants.
VM::ExecResult handleSDiv(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
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

/// @brief Execute the unsigned `udiv` opcode without zero checking.
///
/// Delegates to @ref applyUnsignedDivOrRem so the helper performs width-aware
/// coercions before invoking the supplied lambda to compute the quotient.
VM::ExecResult handleUDiv(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                applyUnsignedDivOrRem(in,
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

/// @brief Execute the signed remainder opcode without zero checking.
///
/// Uses @ref dispatchCheckedSignedBinary to select the proper integer width and
/// reuse the checked division helper for computing the modulo result.
VM::ExecResult handleSRem(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
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

/// @brief Execute the unsigned remainder opcode without zero checking.
///
/// Relies on @ref applyUnsignedDivOrRem to normalise operands and compute the
/// remainder using the provided lambda while leaving zero checking to other
/// opcode variants.
VM::ExecResult handleURem(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                applyUnsignedDivOrRem(in,
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

/// @brief Execute signed division with an explicit zero check.
///
/// Wraps @ref dispatchCheckedSignedBinary with @ref applySignedDiv so divides by
/// zero trigger traps immediately using the standard diagnostic message.
VM::ExecResult handleSDivChk0(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
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

/// @brief Execute unsigned division with divide-by-zero checking.
///
/// Calls @ref applyUnsignedDivOrRem and supplies a lambda that traps when the
/// divisor is zero before computing the quotient.
VM::ExecResult handleUDivChk0(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                applyUnsignedDivOrRem(in,
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

/// @brief Execute signed remainder with divide-by-zero checking.
///
/// Uses @ref dispatchCheckedSignedBinary to compute the remainder while
/// ensuring the divisor is validated via the checked helper.
VM::ExecResult handleSRemChk0(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
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

/// @brief Execute unsigned remainder with divide-by-zero checking.
///
/// Delegates to @ref applyUnsignedDivOrRem, adding a guard that raises the
/// canonical divide-by-zero trap before invoking the modulo lambda.
VM::ExecResult handleURemChk0(VM &vm,
                              Frame &fr,
                              const il::core::Instr &in,
                              const VM::BlockMap &blocks,
                              const il::core::BasicBlock *&bb,
                              size_t &ip)
{
    (void)blocks;
    (void)ip;
    return ops::applyBinary(vm,
                            fr,
                            in,
                            [&, bb](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                applyUnsignedDivOrRem(in,
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

/// @brief Validate array indices for the bounds-checking helper.
///
/// Converts the index operand to the appropriate width and raises an out-of-
/// bounds trap via @ref ops::checked_array_index when necessary. Successful
/// checks simply advance the interpreter without modifying state.
VM::ExecResult handleIdxChk(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    const Slot idxSlot = VMAccess::eval(vm, fr, in.operands[0]);
    const Slot loSlot = VMAccess::eval(vm, fr, in.operands[1]);
    const Slot hiSlot = VMAccess::eval(vm, fr, in.operands[2]);

    auto trapBounds = []() { vm_raise(TrapKind::Bounds); };

    Slot out{};
    switch (in.type.kind)
    {
        case il::core::Type::Kind::I16:
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
        case il::core::Type::Kind::I32:
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

/// @brief Perform bitwise AND on integer operands.
///
/// The helper reuses @ref ops::applyBinary to fetch operands and store the
/// result, applying a straightforward bitwise conjunction in the callback.
VM::ExecResult handleAnd(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
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

/// @brief Perform bitwise OR on integer operands.
///
/// Uses @ref ops::applyBinary to read both operands and writes back their
/// bitwise disjunction.
VM::ExecResult handleOr(VM &vm,
                        Frame &fr,
                        const il::core::Instr &in,
                        const VM::BlockMap &blocks,
                        const il::core::BasicBlock *&bb,
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

/// @brief Perform bitwise XOR on integer operands.
///
/// Delegates operand management to @ref ops::applyBinary and sets the output to
/// the bitwise exclusive-or of the inputs.
VM::ExecResult handleXor(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
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

/// @brief Execute logical left shifts with masking of the shift amount.
///
/// Fetches operands, masks the shift to the 0–63 range, and shifts the value as
/// an unsigned quantity before writing the signed result back into the frame.
VM::ExecResult handleShl(VM &vm,
                         Frame &fr,
                         const il::core::Instr &in,
                         const VM::BlockMap &blocks,
                         const il::core::BasicBlock *&bb,
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
                                out.i64 = static_cast<int64_t>(value << shift);
                            });
}

/// @brief Execute logical right shifts on integer operands.
///
/// Treats the value as unsigned, masks the shift amount, and stores the shifted
/// result so sign bits are not preserved.
VM::ExecResult handleLShr(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
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

/// @brief Execute arithmetic right shifts, preserving sign bits.
///
/// Applies the shift as an unsigned operation and, when the input is negative,
/// fills the vacated high bits to emulate two's-complement arithmetic shifts.
VM::ExecResult handleAShr(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
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
} // namespace il::vm::detail::integer
