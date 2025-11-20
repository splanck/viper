//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/vm/int_ops_cmp.cpp
//
// Summary:
//   Defines the integer comparison opcode handlers for the Viper virtual
//   machine.  Each handler delegates to the shared comparison helper while
//   supplying the predicate that implements the desired IL semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Integer comparison opcode handlers for the VM.
/// @details Provides thin wrappers around @ref ops::applyCompare that evaluate
///          signed and unsigned predicates over @ref Slot values.  The handlers
///          normalise results to the canonical @c i1 representation expected by
///          the interpreter.

#include "vm/OpHandlers_Int.hpp"

namespace il::vm::detail::integer
{
/// @brief Execute the @c icmp.eq opcode.
/// @details Compares the two integer operands for equality using
///          @ref ops::applyCompare and writes the boolean result to the
///          instruction's destination.  Control-flow parameters are ignored
///          because comparison opcodes never branch directly.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution result indicating whether interpretation should continue.
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

/// @brief Execute the @c icmp.ne opcode.
/// @details Emits @c true when the integer operands differ.  Delegates to
///          @ref ops::applyCompare with a predicate that negates the equality
///          comparison.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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

/// @brief Execute the @c scmp.gt opcode.
/// @details Performs a signed greater-than comparison between the two integer
///          operands and stores the resulting predicate value.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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

/// @brief Execute the @c scmp.lt opcode.
/// @details Evaluates the signed less-than predicate over the operands and
///          records the canonical boolean result.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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

/// @brief Execute the @c scmp.le opcode.
/// @details Checks whether the signed left operand is less than or equal to the
///          right operand and writes the boolean outcome.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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

/// @brief Execute the @c scmp.ge opcode.
/// @details Evaluates the signed greater-than-or-equal predicate to populate
///          the boolean result slot.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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

/// @brief Execute the @c ucmp.lt opcode.
/// @details Performs an unsigned comparison by promoting the operands to
///          @c uint64_t and testing for a strict less-than relationship.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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

/// @brief Execute the @c ucmp.le opcode.
/// @details Uses unsigned comparison semantics to determine whether the first
///          operand is less than or equal to the second.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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

/// @brief Execute the @c ucmp.gt opcode.
/// @details Evaluates the unsigned greater-than predicate between the operands
///          and writes the boolean result to the destination slot.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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

/// @brief Execute the @c ucmp.ge opcode.
/// @details Computes the unsigned greater-than-or-equal predicate and stores it
///          via the shared comparison helper.
/// @param vm Virtual machine instance executing the instruction.
/// @param fr Active frame providing operand storage.
/// @param in Instruction describing the comparison operation.
/// @param blocks Map of basic blocks (unused).
/// @param bb Reference to the current block pointer (unused).
/// @param ip Instruction pointer offset within @p bb (unused).
/// @return VM execution control flag.
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
