// MIT License. See LICENSE in the project root for full license information.
// File: src/vm/int_ops_arith.cpp
// Purpose: Implement integer arithmetic, division, and bitwise VM opcode handlers.
// Key invariants: Operations follow IL integer semantics, raising traps on overflow
//                 or divide-by-zero per the specification.
// Links: docs/il-guide.md#reference §Integer Arithmetic, §Bitwise and Shifts

#include "vm/OpHandlers_Int.hpp"

#include "vm/IntOpSupport.hpp"

namespace il::vm::detail::integer
{
VM::ExecResult handleISub(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip)
{
    return handleSub(vm, fr, in, blocks, bb, ip);
}

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
