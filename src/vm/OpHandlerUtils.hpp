//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/OpHandlerUtils.hpp
// Purpose: Shared helper routines for VM opcode handlers.
// Key invariants: Helpers operate on VM frames without leaking references.
// Ownership/Lifetime: Functions mutate frame state in-place without storing globals.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/VM.hpp"

#include "il/core/Instr.hpp"

#include <string>
#include <utility>

namespace il::core
{
struct BasicBlock;
}

namespace il::vm::detail
{
namespace ops
{
/// @brief Perform checked addition using compiler builtins.
/// @tparam T Arithmetic type to operate on.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @param result Pointer receiving the computed value.
/// @return True when the operation overflowed.
/// @note Force-inlined for optimal performance in hot interpreter loops.
template <typename T> [[nodiscard]] inline bool checked_add(T lhs, T rhs, T *result)
{
    return __builtin_add_overflow(lhs, rhs, result);
}

/// @brief Perform checked subtraction using compiler builtins.
/// @tparam T Arithmetic type to operate on.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @param result Pointer receiving the computed value.
/// @return True when the operation overflowed.
/// @note Force-inlined for optimal performance in hot interpreter loops.
template <typename T> [[nodiscard]] inline bool checked_sub(T lhs, T rhs, T *result)
{
    return __builtin_sub_overflow(lhs, rhs, result);
}

/// @brief Perform checked multiplication using compiler builtins.
/// @tparam T Arithmetic type to operate on.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @param result Pointer receiving the computed value.
/// @return True when the operation overflowed.
/// @note Force-inlined for optimal performance in hot interpreter loops.
template <typename T> [[nodiscard]] inline bool checked_mul(T lhs, T rhs, T *result)
{
    return __builtin_mul_overflow(lhs, rhs, result);
}

/// @brief Apply two's complement wrapping semantics to addition.
/// @tparam T Arithmetic type to operate on.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Result of the addition with wrap-around semantics.
template <typename T> inline T wrap_add(T lhs, T rhs)
{
    T result{};
    (void)checked_add(lhs, rhs, &result);
    return result;
}

/// @brief Apply two's complement wrapping semantics to subtraction.
/// @tparam T Arithmetic type to operate on.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Result of the subtraction with wrap-around semantics.
template <typename T> inline T wrap_sub(T lhs, T rhs)
{
    T result{};
    (void)checked_sub(lhs, rhs, &result);
    return result;
}

/// @brief Apply two's complement wrapping semantics to multiplication.
/// @tparam T Arithmetic type to operate on.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Result of the multiplication with wrap-around semantics.
template <typename T> inline T wrap_mul(T lhs, T rhs)
{
    T result{};
    (void)checked_mul(lhs, rhs, &result);
    return result;
}

/// @brief Perform checked addition and invoke a trap policy on overflow.
/// @tparam T Arithmetic type to operate on.
/// @tparam Trap Callable invoked when overflow occurs.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @param result Pointer receiving the computed value.
/// @param trap Policy invoked when overflow occurs.
/// @return True when the result is valid, false if overflow triggered the trap.
template <typename T, typename Trap> inline bool trap_add(T lhs, T rhs, T *result, Trap &&trap)
{
    if (checked_add(lhs, rhs, result))
    {
        std::forward<Trap>(trap)();
        return false;
    }
    return true;
}

/// @brief Perform checked subtraction and invoke a trap policy on overflow.
/// @tparam T Arithmetic type to operate on.
/// @tparam Trap Callable invoked when overflow occurs.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @param result Pointer receiving the computed value.
/// @param trap Policy invoked when overflow occurs.
/// @return True when the result is valid, false if overflow triggered the trap.
template <typename T, typename Trap> inline bool trap_sub(T lhs, T rhs, T *result, Trap &&trap)
{
    if (checked_sub(lhs, rhs, result))
    {
        std::forward<Trap>(trap)();
        return false;
    }
    return true;
}

/// @brief Perform checked multiplication and invoke a trap policy on overflow.
/// @tparam T Arithmetic type to operate on.
/// @tparam Trap Callable invoked when overflow occurs.
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @param result Pointer receiving the computed value.
/// @param trap Policy invoked when overflow occurs.
/// @return True when the result is valid, false if overflow triggered the trap.
template <typename T, typename Trap> inline bool trap_mul(T lhs, T rhs, T *result, Trap &&trap)
{
    if (checked_mul(lhs, rhs, result))
    {
        std::forward<Trap>(trap)();
        return false;
    }
    return true;
}

/// @brief Store the result of an instruction if it produces one.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param val Slot to write into the destination register.
void storeResult(Frame &fr, const il::core::Instr &in, const Slot &val);

/// @brief Internal dispatcher that evaluates operands via the VM.
/// @note Optimized for the dispatch hot path: uses uninitialized Slot for output
///       since the compute/compare functor immediately overwrites it.
struct OperandDispatcher
{
    template <typename Compute>
    static VM::ExecResult runBinary(VM &vm, Frame &fr, const il::core::Instr &in, Compute &&compute)
    {
        const Slot lhs = vm.eval(fr, in.operands[0]);
        const Slot rhs = vm.eval(fr, in.operands[1]);
        Slot out;
        std::forward<Compute>(compute)(out, lhs, rhs);
        storeResult(fr, in, out);
        return {};
    }

    template <typename Compare>
    static VM::ExecResult runCompare(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     Compare &&compare)
    {
        const Slot lhs = vm.eval(fr, in.operands[0]);
        const Slot rhs = vm.eval(fr, in.operands[1]);
        Slot out;
        out.i64 = std::forward<Compare>(compare)(lhs, rhs) ? 1 : 0;
        storeResult(fr, in, out);
        return {};
    }
};

/// @brief Evaluate a binary opcode's operands and run a computation functor.
/// @tparam Compute Callable with signature <tt>void(Slot &, const Slot &, const Slot &)</tt>.
/// @param vm Active virtual machine used for operand evaluation.
/// @param fr Current execution frame.
/// @param in Instruction describing operand locations and result slot.
/// @param compute Functor that writes the computed result into the provided output slot.
/// @return Execution result signalling normal fallthrough.
template <typename Compute>
VM::ExecResult applyBinary(VM &vm, Frame &fr, const il::core::Instr &in, Compute &&compute)
{
    return OperandDispatcher::runBinary(vm, fr, in, std::forward<Compute>(compute));
}

/// @brief Evaluate a binary opcode's operands and run a comparison functor.
/// @tparam Compare Callable with signature <tt>bool(const Slot &, const Slot &)</tt>.
/// @param vm Active virtual machine used for operand evaluation.
/// @param fr Current execution frame.
/// @param in Instruction describing operand locations and result slot.
/// @param compare Functor returning true when the predicate holds.
/// @return Execution result signalling normal fallthrough.
template <typename Compare>
VM::ExecResult applyCompare(VM &vm, Frame &fr, const il::core::Instr &in, Compare &&compare)
{
    return OperandDispatcher::runCompare(vm, fr, in, std::forward<Compare>(compare));
}
} // namespace ops

namespace control
{
Frame::ResumeState *expectResumeToken(Frame &fr, const Slot &slot);

void trapInvalidResume(Frame &fr,
                       const il::core::Instr &in,
                       const il::core::BasicBlock *bb,
                       std::string detail);

const VmError *resolveErrorToken(Frame &fr, const Slot &slot);
} // namespace control
} // namespace il::vm::detail
