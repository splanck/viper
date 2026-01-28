//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/vm/internal/OpHelpers.hpp
// Purpose: Provide reusable helpers for decoding VM operands and emitting common
//          traps used by opcode handlers.
// Invariants: Helpers only operate on operands belonging to the active frame and
//             never retain references beyond the call site.
// Ownership: Functions access VM state through the existing handler access layer
//            without introducing new global state.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/OpHandlerAccess.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

#include <cstddef>
#include <type_traits>

namespace il::vm::internal
{
namespace detail
{
void trapWithMessage(TrapKind kind,
                     const char *message,
                     const il::core::Instr &instr,
                     Frame &frame,
                     const il::core::BasicBlock *block);

template <typename T, typename Enable = void> struct SlotTraits;

template <typename T>
struct SlotTraits<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
{
    [[nodiscard]] static T load(const Slot &slot) noexcept
    {
        return static_cast<T>(slot.i64);
    }

    static void store(Slot &slot, T value) noexcept
    {
        slot.i64 = static_cast<int64_t>(value);
    }
};

template <typename T> struct SlotTraits<T, std::enable_if_t<std::is_floating_point_v<T>>>
{
    [[nodiscard]] static T load(const Slot &slot) noexcept
    {
        return static_cast<T>(slot.f64);
    }

    static void store(Slot &slot, T value) noexcept
    {
        slot.f64 = static_cast<double>(value);
    }
};

template <> struct SlotTraits<bool, void>
{
    [[nodiscard]] static bool load(const Slot &slot) noexcept
    {
        return slot.i64 != 0;
    }

    static void store(Slot &slot, bool value) noexcept
    {
        slot.i64 = value ? 1 : 0;
    }
};

} // namespace detail

/// @brief Evaluate operand @p index as type @p T using the active VM execution state.
/// @tparam T Destination type to read from the operand slot.
/// @param vm Virtual machine used for operand evaluation.
/// @param fr Active frame containing the operand slots.
/// @param instr Instruction describing the operand locations.
/// @param index Operand index to decode.
/// @return Operand value converted to @p T.
/// @note This helper simply materialises the operand slot and casts the stored value.
template <typename T>
[[nodiscard]] inline T readOperand(VM &vm, Frame &fr, const il::core::Instr &instr, size_t index)
{
    const Slot slot = il::vm::detail::VMAccess::eval(vm, fr, instr.operands[index]);
    return detail::SlotTraits<T>::load(slot);
}

/// @brief Store @p value into the destination slot for @p instr.
/// @tparam T Value type written into the result slot.
/// @param fr Active frame receiving the result.
/// @param instr Instruction whose destination slot is updated.
/// @param value Typed value to persist.
/// @note Uses uninitialized Slot to avoid redundant zero-initialization
///       since SlotTraits::store immediately overwrites the relevant field.
template <typename T> inline void writeResult(Frame &fr, const il::core::Instr &instr, T value)
{
    Slot slot;
    detail::SlotTraits<T>::store(slot, value);
    il::vm::detail::ops::storeResult(fr, instr, slot);
}

/// @brief Execute a binary opcode by evaluating both operands as @p T.
/// @tparam T Type used for operand decoding and result storage.
/// @tparam Compute Callable invoked with the decoded operands.
/// @param vm Active virtual machine.
/// @param fr Current execution frame.
/// @param instr Instruction providing operand metadata.
/// @param compute Functor returning the value to store in the destination slot.
/// @return VM execution result signalling normal fallthrough.
/// @note The helper leaves control-flow metadata untouched; callers update block/ip if needed.
template <typename T, typename Compute>
inline VM::ExecResult binaryOp(VM &vm, Frame &fr, const il::core::Instr &instr, Compute &&compute)
{
    const T lhs = readOperand<T>(vm, fr, instr, 0);
    const T rhs = readOperand<T>(vm, fr, instr, 1);
    writeResult<T>(fr, instr, std::forward<Compute>(compute)(lhs, rhs));
    return {};
}

/// @brief Emit a divide-by-zero trap with a standardised diagnostic payload.
/// @param instr Instruction responsible for the trap.
/// @param frame Active frame providing diagnostic context.
/// @param block Current basic block (may be null).
/// @param message Human-readable message describing the fault.
inline void trapDivideByZero(const il::core::Instr &instr,
                             Frame &frame,
                             const il::core::BasicBlock *block,
                             const char *message)
{
    detail::trapWithMessage(TrapKind::DivideByZero, message, instr, frame, block);
}

/// @brief Emit an overflow trap using a shared diagnostic formatter.
/// @param instr Instruction responsible for the trap.
/// @param frame Active frame providing diagnostic context.
/// @param block Current basic block (may be null).
/// @param message Human-readable message describing the fault.
inline void trapOverflow(const il::core::Instr &instr,
                         Frame &frame,
                         const il::core::BasicBlock *block,
                         const char *message)
{
    detail::trapWithMessage(TrapKind::Overflow, message, instr, frame, block);
}

/// @brief Guard against zero divisors before performing a division-like operation.
/// @tparam T Operand type supporting equality comparison with zero.
/// @param divisor Value checked for zero.
/// @param instr Instruction responsible for the operation.
/// @param frame Active frame providing diagnostic context.
/// @param block Current basic block (may be null).
/// @param message Human-readable message describing the trap condition.
/// @return @c true when @p divisor is non-zero; @c false after emitting a trap.
template <typename T>
[[nodiscard]] inline bool ensureNonZero(T divisor,
                                        const il::core::Instr &instr,
                                        Frame &frame,
                                        const il::core::BasicBlock *block,
                                        const char *message)
{
    if (divisor == static_cast<T>(0))
    {
        trapDivideByZero(instr, frame, block, message);
        return false;
    }
    return true;
}

} // namespace il::vm::internal
