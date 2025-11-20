//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the integer arithmetic, division, and bitwise opcode handlers used
// by the virtual machine.  Each handler wraps the shared helper utilities in
// vm/IntOpSupport.hpp so overflow detection, divide-by-zero traps, and result
// normalisation all follow the IL specification verbatim.
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Int.hpp"

#include "vm/IntOpSupport.hpp"

#include <cassert>

/// @file
/// @brief Integer arithmetic opcode handlers for the VM interpreter.
/// @details The functions in this translation unit evaluate arithmetic and
///          bitwise instructions over @ref Slot values contained within an
///          active @ref il::vm::Frame.  They delegate to shared helpers for
///          operand evaluation and trap reporting, allowing the opcode metadata
///          generated from Opcode.def to remain the single source of truth for
///          semantics such as overflow behaviour.

namespace il::vm::detail::integer
{
/// @brief Execute the @c isub opcode using the canonical subtraction helper.
/// @details Defers to @ref handleSub from @ref vm::ops so addition overflow and
///          diagnostics remain consistent with other subtraction forms.  Control
///          flow metadata parameters are unused because arithmetic opcodes never
///          adjust the interpreter's block or instruction pointers directly.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction being interpreted.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result describing whether the VM should continue running.
VM::ExecResult handleISub(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip)
{
    return handleSub(vm, fr, in, blocks, bb, ip);
}

/// @brief Execute the @c iadd.ovf opcode and trap on overflow.
/// @details Evaluates both operands via @ref ops::applyBinary, then invokes
///          @ref dispatchOverflowingBinary with @ref ops::checked_add to compute
///          the result.  When the addition exceeds the representable range of
///          the destination type the helper signals an overflow trap using the
///          instruction's source location.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the addition.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c isub.ovf opcode and trap on overflow.
/// @details Mirrors @ref handleIAddOvf but performs subtraction via
///          @ref ops::checked_sub.  Overflow detection translates into an
///          @ref TrapKind::Overflow trap emitted through
///          @ref dispatchOverflowingBinary.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the subtraction.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c imul.ovf opcode and trap on overflow.
/// @details Delegates to @ref ops::checked_mul to multiply integer operands with
///          full overflow detection.  The helper emits traps when the product
///          exceeds the destination width, aligning behaviour with the IL
///          specification's overflow semantics.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the multiplication.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c sdiv opcode with signed semantics.
/// @details Utilises @ref dispatchCheckedSignedBinary with the
///          @ref applySignedDiv helpers to honour divide-by-zero traps and the
///          IL's signed division rounding rules.  Control-flow metadata
///          parameters are unused.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the division.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c udiv opcode using unsigned semantics.
/// @details Applies @ref applyUnsignedDivOrRem with a division functor so that
///          divide-by-zero conditions trap consistently.  The helper manages the
///          destination slot and range checking for all unsigned integer widths.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the division.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c srem opcode that computes signed remainders.
/// @details Uses @ref dispatchCheckedSignedBinary with
///          @ref applySignedRem to propagate divide-by-zero traps and to mirror
///          the IL's remainder semantics for negative operands.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the remainder operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c urem opcode with unsigned semantics.
/// @details Delegates to @ref applyUnsignedDivOrRem providing a modulus functor
///          so divide-by-zero traps and unsigned range handling remain
///          consistent across opcode variants.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the remainder operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c sdiv.chk0 opcode, trapping on divide-by-zero.
/// @details Reuses the signed division helper but ensures any zero divisor
///          triggers a @ref TrapKind::DomainError via
///          @ref applyCheckedDiv.  Control-flow metadata is unused because the
///          instruction does not branch.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the division.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c udiv.chk0 opcode, trapping on divide-by-zero.
/// @details Mirrors @ref handleUDiv but uses the range-checking helper to emit a
///          trap message specific to the @c .chk0 variant, ensuring diagnostics
///          mention the originating opcode.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the division.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c srem.chk0 opcode, trapping on divide-by-zero.
/// @details Ensures the signed remainder operation traps when the divisor is
///          zero by funnelling execution through @ref applyCheckedRem.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the remainder operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c urem.chk0 opcode, trapping on divide-by-zero.
/// @details Calls @ref applyUnsignedDivOrRem with a modulus functor and a
///          diagnostic message tailored to the @c .chk0 variant.  Successfully
///          computed results are written back to the destination slot.
/// @param vm Virtual machine context executing the opcode.
/// @param fr Active frame that stores operands and results.
/// @param in IL instruction describing the remainder operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer for diagnostics.
/// @param ip Instruction index within the block (unused).
/// @return Execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c idx.chk opcode that validates array indices.
/// @details Evaluates index, lower-bound, and upper-bound operands and ensures
///          the index lies within the closed range [lo, hi].  When the index is
///          valid the value is normalised relative to the lower bound and stored
///          in the destination slot; otherwise a bounds trap is raised.
/// @param vm Virtual machine instance executing the opcode.
/// @param fr Active frame supplying operand slots.
/// @param in Instruction containing the operands and destination type.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result describing whether interpretation should continue.
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

/// @brief Execute the @c and opcode implementing bitwise conjunction.
/// @details Applies @ref ops::applyBinary with a lambda that combines operands
///          using the bitwise AND operator.  Control-flow metadata is unused.
/// @param vm Virtual machine instance executing the opcode.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result describing whether interpretation should continue.
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

/// @brief Execute the @c or opcode implementing bitwise disjunction.
/// @details Uses @ref ops::applyBinary with a lambda that performs a bitwise OR
///          on the operands before storing the result slot.
/// @param vm Virtual machine instance executing the opcode.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result describing whether interpretation should continue.
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

/// @brief Execute the @c xor opcode implementing bitwise exclusive-or.
/// @details Applies @ref ops::applyBinary with a lambda that computes the XOR of
///          the operands, storing the result in the destination slot.
/// @param vm Virtual machine instance executing the opcode.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result describing whether interpretation should continue.
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

/// @brief Execute the @c shl opcode implementing left shifts.
/// @details Normalises the shift amount to the low six bits before shifting the
///          operand, mirroring the IL specification that masks excessive shift
///          counts.  The computation is performed in unsigned space to avoid
///          undefined behaviour before converting back to the destination type.
/// @param vm Virtual machine instance executing the opcode.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result describing whether interpretation should continue.
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

/// @brief Execute the @c lshr opcode implementing logical right shifts.
/// @details Masks the shift count to the low six bits and treats the operand as
///          unsigned so high bits are filled with zero during the shift.
/// @param vm Virtual machine instance executing the opcode.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result describing whether interpretation should continue.
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

/// @brief Execute the @c ashr opcode implementing arithmetic right shifts.
/// @details Masks the shift amount, preserves the sign bit when shifting
///          negative values, and writes the sign-extended result back to the
///          destination slot.  Zero shifts return the unmodified operand.
/// @param vm Virtual machine instance executing the opcode.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the operation.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction index within the block (unused).
/// @return Execution result describing whether interpretation should continue.
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
                                    // Defensive check: shift must be in range [1, 63] to avoid UB
                                    // in the expression (64U - shift). The mask at line 692 ensures
                                    // this, but we assert it explicitly for safety.
                                    assert(shift > 0 && shift < 64 &&
                                           "shift must be in range [1, 63]");
                                    const uint64_t mask = (~uint64_t{0}) << (64U - shift);
                                    shifted |= mask;
                                }
                                out.i64 = static_cast<int64_t>(shifted);
                            });
}
} // namespace il::vm::detail::integer
