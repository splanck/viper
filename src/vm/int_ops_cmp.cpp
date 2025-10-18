//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements integer comparison opcode handlers for the VM.  Each handler
// provides a lambda predicate to the shared `applyCompare` helper, which
// manages operand fetching, short-circuiting on traps, and result materialisation.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief VM opcode handlers for integer comparisons.
/// @details Defines the predicates backing signed and unsigned comparison
///          opcodes.  The heavy lifting is performed by `ops::applyCompare`,
///          which ensures consistent evaluation order and result canonicalisation.

#include "vm/OpHandlers_Int.hpp"

namespace il::vm::detail::integer
{
/// @brief Execute the `icmp.eq` opcode.
///
/// @details Compares two integer operands for equality by forwarding the
///          predicate to `applyCompare`.  Control-flow parameters are ignored
///          because equality is a pure computation.
VM::ExecResult handleICmpEq(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
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

/// @brief Execute the `icmp.ne` opcode.
///
/// @details Produces true when the operands differ.  Delegates comparison to
///          `applyCompare` so operand loading and boolean normalisation remain
///          consistent with other handlers.
VM::ExecResult handleICmpNe(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
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

/// @brief Execute the signed greater-than comparison (`scmp.gt`).
///
/// @details Interprets the operands as signed 64-bit integers and signals true
///          when the left operand exceeds the right operand.
VM::ExecResult handleSCmpGT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm, fr, in, [](const Slot &lhsVal, const Slot &rhsVal) { return lhsVal.i64 > rhsVal.i64; });
}

/// @brief Execute the signed less-than comparison (`scmp.lt`).
///
/// @details Returns true when the signed left operand is smaller than the right
///          operand.
VM::ExecResult handleSCmpLT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm, fr, in, [](const Slot &lhsVal, const Slot &rhsVal) { return lhsVal.i64 < rhsVal.i64; });
}

/// @brief Execute the signed less-or-equal comparison (`scmp.le`).
///
/// @details Reuses the signed ordering semantics while allowing equality as a
///          success condition.
VM::ExecResult handleSCmpLE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
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

/// @brief Execute the signed greater-or-equal comparison (`scmp.ge`).
///
/// @details Treats the operands as signed integers and yields true when the
///          left operand is not smaller than the right operand.
VM::ExecResult handleSCmpGE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
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

/// @brief Execute the unsigned less-than comparison (`ucmp.lt`).
///
/// @details Casts operands to unsigned 64-bit integers before performing the
///          comparison so negative values wrap according to IL semantics.
VM::ExecResult handleUCmpLT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm,
        fr,
        in,
        [](const Slot &lhsVal, const Slot &rhsVal)
        { return static_cast<uint64_t>(lhsVal.i64) < static_cast<uint64_t>(rhsVal.i64); });
}

/// @brief Execute the unsigned less-or-equal comparison (`ucmp.le`).
///
/// @details Mirrors `handleUCmpLT` but allows equality to succeed.
VM::ExecResult handleUCmpLE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm,
        fr,
        in,
        [](const Slot &lhsVal, const Slot &rhsVal)
        { return static_cast<uint64_t>(lhsVal.i64) <= static_cast<uint64_t>(rhsVal.i64); });
}

/// @brief Execute the unsigned greater-than comparison (`ucmp.gt`).
///
/// @details Uses unsigned comparisons so that wraparound semantics match the IL
///          specification.
VM::ExecResult handleUCmpGT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm,
        fr,
        in,
        [](const Slot &lhsVal, const Slot &rhsVal)
        { return static_cast<uint64_t>(lhsVal.i64) > static_cast<uint64_t>(rhsVal.i64); });
}

/// @brief Execute the unsigned greater-or-equal comparison (`ucmp.ge`).
///
/// @details Similar to `handleUCmpGT` but allows equality to produce true.
VM::ExecResult handleUCmpGE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    return ops::applyCompare(
        vm,
        fr,
        in,
        [](const Slot &lhsVal, const Slot &rhsVal)
        { return static_cast<uint64_t>(lhsVal.i64) >= static_cast<uint64_t>(rhsVal.i64); });
}
} // namespace il::vm::detail::integer
