// File: src/vm/fp_ops.cpp
// Purpose: Implement VM handlers for floating-point math and conversions.
// License: MIT License (see LICENSE).
// Key invariants: Floating-point operations follow IEEE-754 semantics of host double type.
// Ownership/Lifetime: Handlers operate on frame-local slots without retaining references.
// Assumptions: Host doubles implement IEEE-754 binary64 semantics and frames mutate only via ops::storeResult.
// Links: docs/il-spec.md

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "vm/OpHandlerUtils.hpp"

using namespace il::core;

namespace il::vm::detail
{
/// @brief Add two floating-point values and store the IEEE-754 sum.
/// @details Relies on host binary64 addition so NaNs propagate and infinities behave per IEEE-754.
/// The handler mutates the frame only by writing the result slot via ops::storeResult.
VM::ExecResult OpHandlers::handleFAdd(VM &vm,
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
VM::ExecResult OpHandlers::handleFSub(VM &vm,
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
VM::ExecResult OpHandlers::handleFMul(VM &vm,
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
VM::ExecResult OpHandlers::handleFDiv(VM &vm,
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
VM::ExecResult OpHandlers::handleFCmpEQ(VM &vm,
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
VM::ExecResult OpHandlers::handleFCmpNE(VM &vm,
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
VM::ExecResult OpHandlers::handleFCmpGT(VM &vm,
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
VM::ExecResult OpHandlers::handleFCmpLT(VM &vm,
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
VM::ExecResult OpHandlers::handleFCmpLE(VM &vm,
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
VM::ExecResult OpHandlers::handleFCmpGE(VM &vm,
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
VM::ExecResult OpHandlers::handleSitofp(VM &vm,
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
    Slot out{};
    out.f64 = static_cast<double>(value.i64);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Convert a floating-point value to a signed 64-bit integer using truncation toward zero.
/// @details Assumes the source is a finite value representable in int64_t; NaNs or out-of-range
/// values exhibit host-defined (potentially undefined) behaviour. The handler mutates the frame
/// solely by storing the result slot.
VM::ExecResult OpHandlers::handleFptosi(VM &vm,
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
    Slot out{};
    out.i64 = static_cast<int64_t>(value.f64);
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail

