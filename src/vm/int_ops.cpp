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

#include <limits>

using namespace il::core;

namespace il::vm::detail
{
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
                            [&](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                long long result{};
                                if (__builtin_add_overflow(lhsVal.i64, rhsVal.i64, &result))
                                {
                                    RuntimeBridge::trap(
                                        "integer overflow in iadd.ovf", in.loc, fr.func->name,
                                        bb ? bb->label : "");
                                }
                                out.i64 = result;
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
                            [&](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                long long result{};
                                if (__builtin_sub_overflow(lhsVal.i64, rhsVal.i64, &result))
                                {
                                    RuntimeBridge::trap(
                                        "integer overflow in isub.ovf", in.loc, fr.func->name,
                                        bb ? bb->label : "");
                                }
                                out.i64 = result;
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
                            [&](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                long long result{};
                                if (__builtin_mul_overflow(lhsVal.i64, rhsVal.i64, &result))
                                {
                                    RuntimeBridge::trap(
                                        "integer overflow in imul.ovf", in.loc, fr.func->name,
                                        bb ? bb->label : "");
                                }
                                out.i64 = result;
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
                            [&](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                const auto divisor = rhsVal.i64;
                                if (divisor == 0)
                                {
                                    RuntimeBridge::trap(
                                        "divide by zero in sdiv.chk0", in.loc, fr.func->name,
                                        bb ? bb->label : "");
                                }
                                const auto dividend = lhsVal.i64;
                                if (dividend == std::numeric_limits<int64_t>::min() && divisor == -1)
                                {
                                    RuntimeBridge::trap(
                                        "integer overflow in sdiv.chk0", in.loc, fr.func->name,
                                        bb ? bb->label : "");
                                }
                                out.i64 = dividend / divisor;
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
                            [&](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                const auto divisor = static_cast<uint64_t>(rhsVal.i64);
                                if (divisor == 0)
                                {
                                    RuntimeBridge::trap(
                                        "divide by zero in udiv.chk0", in.loc, fr.func->name,
                                        bb ? bb->label : "");
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
                            [&](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                const auto divisor = rhsVal.i64;
                                if (divisor == 0)
                                {
                                    RuntimeBridge::trap(
                                        "divide by zero in srem.chk0", in.loc, fr.func->name,
                                        bb ? bb->label : "");
                                }
                                const auto dividend = lhsVal.i64;
                                if (dividend == std::numeric_limits<int64_t>::min() && divisor == -1)
                                {
                                    RuntimeBridge::trap(
                                        "integer overflow in srem.chk0", in.loc, fr.func->name,
                                        bb ? bb->label : "");
                                }
                                out.i64 = dividend % divisor;
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
                            [&](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                            {
                                const auto divisor = static_cast<uint64_t>(rhsVal.i64);
                                if (divisor == 0)
                                {
                                    RuntimeBridge::trap(
                                        "divide by zero in urem.chk0", in.loc, fr.func->name,
                                        bb ? bb->label : "");
                                }
                                const auto dividend = static_cast<uint64_t>(lhsVal.i64);
                                out.i64 = static_cast<int64_t>(dividend % divisor);
                            });
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

