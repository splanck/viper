// MIT License. See LICENSE in the project root for full license information.
// File: src/vm/int_ops.cpp
// Purpose: Implement VM handlers for integer arithmetic, bitwise logic, comparisons, and
//          1-bit conversions.
// Key invariants: Results use 64-bit two's complement semantics consistent with the IL
//                 reference, and handlers only mutate the current frame.
// Links: docs/il-reference.md §Integer Arithmetic, §Bitwise and Shifts, §Comparisons,
//        §Conversions

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "vm/OpHandlerUtils.hpp"

using namespace il::core;

namespace il::vm::detail
{
namespace
{
    /// @brief Apply a binary integer VM operation using evaluated operands.
    /// @param fr Current frame that will receive the result via @c ops::storeResult.
    /// @param in Instruction describing the opcode and destination slot.
    /// @param lhs Pre-evaluated left operand slot.
    /// @param rhs Pre-evaluated right operand slot.
    /// @param compute Callable writing the resulting integer into @p out with the
    ///        IL's wrap-around semantics for `i64` values (see docs/il-reference.md
    ///        §Integer Arithmetic and §Types).
    /// @return Execution result signalling normal fallthrough.
    /// @note The helper isolates operand fetching so individual opcode handlers only
    ///       specify the arithmetic rule while maintaining the frame mutation contract.
    template <typename Compute>
    VM::ExecResult applyBinary(Frame &fr,
                               const Instr &in,
                               const Slot &lhs,
                               const Slot &rhs,
                               Compute compute)
    {
        Slot out{};
        compute(out, lhs, rhs);
        ops::storeResult(fr, in, out);
        return {};
    }

    /// @brief Apply an integer comparison producing a canonical `i1` result.
    /// @param fr Current frame that will be updated with a 0/1 boolean outcome.
    /// @param in Instruction describing the comparison opcode and destination slot.
    /// @param lhs Pre-evaluated left operand slot.
    /// @param rhs Pre-evaluated right operand slot.
    /// @param compare Callable returning @c true when the comparison succeeds.
    /// @return Execution result signalling normal fallthrough.
    /// @note The result is canonicalised to the IL boolean domain (`0` or `1`) per
    ///       docs/il-reference.md §Comparisons.
    template <typename Compare>
    VM::ExecResult applyCompare(Frame &fr,
                                const Instr &in,
                                const Slot &lhs,
                                const Slot &rhs,
                                Compare compare)
    {
        Slot out{};
        out.i64 = compare(lhs, rhs) ? 1 : 0;
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
///       wrap-around, matching docs/il-reference.md §Integer Arithmetic and the
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 + rhsVal.i64; });
}

/// @brief Interpret the `sub` opcode for 64-bit integers.
/// @note Operand evaluation and frame updates mirror @ref OpHandlers::handleAdd, with subtraction
///       obeying two's complement wrap semantics per docs/il-reference.md §Integer Arithmetic.
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 - rhsVal.i64; });
}

/// @brief Interpret the `mul` opcode for 64-bit integers.
/// @note Multiplication uses the same operand handling helpers as addition, wraps
///       modulo 2^64 per docs/il-reference.md §Integer Arithmetic, and stores the
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 * rhsVal.i64; });
}

/// @brief Interpret the `xor` opcode for 64-bit integers.
/// @note Operands are evaluated via @c vm.eval and the bitwise result is stored back
///       into the destination register, matching docs/il-reference.md §Bitwise and Shifts.
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 ^ rhsVal.i64; });
}

/// @brief Interpret the `shl` opcode for integer left shifts.
/// @note The shift count is taken from the second operand; well-formed IL keeps it within
///       [0, 63] so the host operation remains defined, and the result is written back
///       to the frame (docs/il-reference.md §Bitwise and Shifts).
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyBinary(fr, in, lhs, rhs, [](Slot &out, const Slot &lhsVal, const Slot &rhsVal)
                                           { out.i64 = lhsVal.i64 << rhsVal.i64; });
}

/// @brief Interpret the `icmp_eq` opcode for integer equality comparisons.
/// @note Produces a canonical `i1` value (0 or 1) stored via @c ops::storeResult,
///       following docs/il-reference.md §Comparisons.
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 == rhsVal.i64; });
}

/// @brief Interpret the `icmp_ne` opcode for integer inequality comparisons.
/// @note Semantics mirror @ref OpHandlers::handleICmpEq with negated predicate per docs/il-reference.md §Comparisons.
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 != rhsVal.i64; });
}

/// @brief Interpret the `scmp_gt` opcode for signed greater-than comparisons.
/// @note Reads both operands as signed 64-bit integers and stores a canonical `i1`
///       result, consistent with docs/il-reference.md §Comparisons.
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 > rhsVal.i64; });
}

/// @brief Interpret the `scmp_lt` opcode for signed less-than comparisons.
/// @note Shares operand evaluation and storage behaviour with other comparison handlers,
///       producing canonical booleans per docs/il-reference.md §Comparisons.
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 < rhsVal.i64; });
}

/// @brief Interpret the `scmp_le` opcode for signed less-or-equal comparisons.
/// @note Uses signed ordering per docs/il-reference.md §Comparisons and returns a
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 <= rhsVal.i64; });
}

/// @brief Interpret the `scmp_ge` opcode for signed greater-or-equal comparisons.
/// @note Completes the signed comparison set defined in docs/il-reference.md §Comparisons
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
    Slot lhs = vm.eval(fr, in.operands[0]);
    Slot rhs = vm.eval(fr, in.operands[1]);
    return applyCompare(fr, in, lhs, rhs, [](const Slot &lhsVal, const Slot &rhsVal)
                                          { return lhsVal.i64 >= rhsVal.i64; });
}

/// @brief Interpret the `trunc1`/`zext1` opcodes that normalise between `i1` and `i64`.
/// @note The operand is masked to the least-significant bit so the stored value is a
///       canonical boolean per docs/il-reference.md §Conversions.
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

