//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the floating-point opcode handlers used by the virtual machine.
// Each helper interprets the operands stored in the active frame, performs the
// requested IEEE-754 operation, and writes the result back through the shared
// op-handler utilities while honouring the IL specification's trapping rules.
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers_Float.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

/// @file
/// @brief Floating-point opcode handlers and helpers for the VM interpreter.
/// @details The routines here all follow the same pattern: evaluate operand
///          slots via the shared `ops::apply*` helpers, perform the floating
///          point computation using host IEEE-754 semantics, and finally write
///          the result back to the destination slot.  Checked conversions rely
///          on local utilities that raise structured traps through the runtime
///          bridge when the source value cannot be represented exactly.

using namespace il::core;

namespace il::vm::detail::floating
{
namespace
{
constexpr double kUint64Boundary = 18446744073709551616.0; ///< 2^64, sentinel for overflow.

Type::Kind tempType(const Frame &fr, unsigned id)
{
    if (id < fr.regTypes.size())
        return fr.regTypes[id];
    return Type::Kind::Void;
}

float operandAsF32(const Frame &fr, const Instr &in, size_t index, const Slot &slot)
{
    const auto &value = in.operands[index];
    switch (value.kind)
    {
        case Value::Kind::ConstFloat:
            return static_cast<float>(value.f64);
        case Value::Kind::ConstInt:
            return static_cast<float>(value.i64);
        case Value::Kind::NullPtr:
            return 0.0f;
        case Value::Kind::Temp:
        {
            const auto kind = tempType(fr, value.id);
            if (kind == Type::Kind::F64)
                return static_cast<float>(slot.f64);
            return slot.f32;
        }
        default:
            return slot.f32;
    }
}

double operandAsF64(const Frame &fr, const Instr &in, size_t index, const Slot &slot)
{
    const auto &value = in.operands[index];
    switch (value.kind)
    {
        case Value::Kind::ConstFloat:
            return value.f64;
        case Value::Kind::ConstInt:
            return static_cast<double>(value.i64);
        case Value::Kind::NullPtr:
            return 0.0;
        case Value::Kind::Temp:
        {
            const auto kind = tempType(fr, value.id);
            if (kind == Type::Kind::F32)
                return static_cast<double>(slot.f32);
            return slot.f64;
        }
        default:
            return slot.f64;
    }
}

bool operandsPreferF32(const Frame &fr, const Instr &in)
{
    const auto first = in.operands.empty() ? Type::Kind::Void
                                           : (in.operands[0].kind == Value::Kind::Temp
                                                  ? tempType(fr, in.operands[0].id)
                                                  : Type::Kind::Void);
    const auto second = in.operands.size() > 1 && in.operands[1].kind == Value::Kind::Temp
                            ? tempType(fr, in.operands[1].id)
                            : Type::Kind::Void;
    return first == Type::Kind::F32 || second == Type::Kind::F32;
}

template <typename PredF32, typename PredF64>
bool runFloatCompare(const Frame &fr,
                     const Instr &in,
                     const Slot &lhsSlot,
                     const Slot &rhsSlot,
                     PredF32 &&predF32,
                     PredF64 &&predF64)
{
    if (operandsPreferF32(fr, in))
    {
        const float lhs = operandAsF32(fr, in, 0, lhsSlot);
        const float rhs = operandAsF32(fr, in, 1, rhsSlot);
        return std::forward<PredF32>(predF32)(lhs, rhs);
    }
    const double lhs = operandAsF64(fr, in, 0, lhsSlot);
    const double rhs = operandAsF64(fr, in, 1, rhsSlot);
    return std::forward<PredF64>(predF64)(lhs, rhs);
}

/// @brief Round @p operand to the nearest unsigned 64-bit integer or raise a trap.
/// @details Implements the semantics of `cast.fp_to_ui.rte.chk` in four stages:
///          1. Validate that the operand is finite and non-negative, trapping
///             with @ref TrapKind::InvalidCast when the preconditions fail.
///          2. Reject magnitudes greater than or equal to 2^64 by emitting an
///             @ref TrapKind::Overflow diagnostic via the runtime bridge.
///          3. Use @ref std::modf to split the operand into integer and
///             fractional parts, then apply banker’s rounding (ties to even).
///          4. Perform a final overflow check before casting to `uint64_t` so
///             rounding does not silently wrap.
///          Any trap is raised through @ref RuntimeBridge::trap so diagnostics
///          include the instruction location and owning function.
/// @param operand Floating-point value requested for conversion.
/// @param in Instruction descriptor providing source information for diagnostics.
/// @param fr Active frame containing the function and destination slot.
/// @param bb Basic block label used in diagnostic output; may be null.
/// @return Rounded unsigned integer when conversion succeeds without trapping.
[[nodiscard]] uint64_t castFpToUiRoundedOrTrap(double operand,
                                               const Instr &in,
                                               Frame &fr,
                                               const BasicBlock *bb)
{
    constexpr const char *kInvalidOperandMessage = "invalid fp operand in cast.fp_to_ui.rte.chk";
    constexpr const char *kOverflowMessage = "fp overflow in cast.fp_to_ui.rte.chk";

    auto trap = [&](TrapKind kind, const char *message)
    { RuntimeBridge::trap(kind, message, in.loc, fr.func->name, bb ? bb->label : ""); };

    if (!std::isfinite(operand) || std::signbit(operand))
    {
        trap(TrapKind::InvalidCast, kInvalidOperandMessage);
    }

    if (operand >= kUint64Boundary)
    {
        trap(TrapKind::Overflow, kOverflowMessage);
    }

    double integral = 0.0;
    const double fractional = std::modf(operand, &integral);

    if (fractional > 0.5)
    {
        integral += 1.0;
    }
    else if (fractional == 0.5)
    {
        if (std::fmod(integral, 2.0) != 0.0)
        {
            integral += 1.0;
        }
    }

    if (integral >= kUint64Boundary)
    {
        trap(TrapKind::Overflow, kOverflowMessage);
    }

    return static_cast<uint64_t>(integral);
}
} // namespace

/// @brief Execute the `fadd` opcode by summing two floating-point operands.
/// @details Evaluates operand slots with @ref VMAccess::eval so lazy values are
///          materialised, converts each operand to either @c float or
///          @c double depending on the active instruction type, performs the
///          addition with host IEEE-754 semantics, and writes the result via
///          @ref ops::storeResult.  Control-flow state remains untouched because
///          the instruction is purely arithmetic.
/// @param vm Virtual machine orchestrating execution (unused).
/// @param fr Active frame containing operand and result slots.
/// @param in Instruction describing the addition.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFAdd(VM &vm,
                          Frame &fr,
                          const Instr &in,
                          const VM::BlockMap &blocks,
                          const BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    Slot out{};
    if (in.type.kind == Type::Kind::F32)
    {
        const float lhs = operandAsF32(fr, in, 0, lhsSlot);
        const float rhs = operandAsF32(fr, in, 1, rhsSlot);
        out.f32 = lhs + rhs;
    }
    else
    {
        const double lhs = operandAsF64(fr, in, 0, lhsSlot);
        const double rhs = operandAsF64(fr, in, 1, rhsSlot);
        out.f64 = lhs + rhs;
    }
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fsub` opcode by subtracting two floating-point operands.
/// @details Materialises operands through @ref VMAccess::eval, converts them to
///          the width dictated by the instruction's result type, performs the
///          subtraction using IEEE-754 rules (preserving signed zero and NaN
///          propagation), and stores the result back through
///          @ref ops::storeResult.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame containing operand and result storage.
/// @param in Instruction describing the subtraction.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFSub(VM &vm,
                          Frame &fr,
                          const Instr &in,
                          const VM::BlockMap &blocks,
                          const BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    Slot out{};
    if (in.type.kind == Type::Kind::F32)
    {
        const float lhs = operandAsF32(fr, in, 0, lhsSlot);
        const float rhs = operandAsF32(fr, in, 1, rhsSlot);
        out.f32 = lhs - rhs;
    }
    else
    {
        const double lhs = operandAsF64(fr, in, 0, lhsSlot);
        const double rhs = operandAsF64(fr, in, 1, rhsSlot);
        out.f64 = lhs - rhs;
    }
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fmul` opcode by multiplying two floating-point operands.
/// @details Fetches operand slots via @ref VMAccess::eval, widens them to
///          @c float or @c double as required, multiplies the values with IEEE-754
///          semantics, and stores the product in the destination slot.  The
///          handler never mutates control-flow metadata because the operation is
///          side-effect free.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the multiplication.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFMul(VM &vm,
                          Frame &fr,
                          const Instr &in,
                          const VM::BlockMap &blocks,
                          const BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    Slot out{};
    if (in.type.kind == Type::Kind::F32)
    {
        const float lhs = operandAsF32(fr, in, 0, lhsSlot);
        const float rhs = operandAsF32(fr, in, 1, rhsSlot);
        out.f32 = lhs * rhs;
    }
    else
    {
        const double lhs = operandAsF64(fr, in, 0, lhsSlot);
        const double rhs = operandAsF64(fr, in, 1, rhsSlot);
        out.f64 = lhs * rhs;
    }
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fdiv` opcode by dividing two floating-point operands.
/// @details Materialises operands with @ref VMAccess::eval, narrows them to the
///          instruction's precision, performs IEEE-754 division (surfacing
///          infinities and NaNs exactly as specified), and writes the quotient to
///          the destination slot.  Control-flow bookkeeping parameters remain
///          untouched because division cannot branch.
/// @param vm Virtual machine orchestrating execution (unused).
/// @param fr Active frame providing operand and destination storage.
/// @param in Instruction describing the division.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFDiv(VM &vm,
                          Frame &fr,
                          const Instr &in,
                          const VM::BlockMap &blocks,
                          const BasicBlock *&bb,
                          size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    Slot out{};
    if (in.type.kind == Type::Kind::F32)
    {
        const float lhs = operandAsF32(fr, in, 0, lhsSlot);
        const float rhs = operandAsF32(fr, in, 1, rhsSlot);
        out.f32 = lhs / rhs;
    }
    else
    {
        const double lhs = operandAsF64(fr, in, 0, lhsSlot);
        const double rhs = operandAsF64(fr, in, 1, rhsSlot);
        out.f64 = lhs / rhs;
    }
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fcmp.eq` opcode and record whether operands compare equal.
/// @details Evaluates operand slots with @ref VMAccess::eval, routes the
///          comparison through @ref runFloatCompare so both precisions are
///          handled uniformly, and finally writes the boolean result via
///          @ref ops::storeResult.  IEEE-754 semantics apply: NaN operands force
///          a false result while signed zeros compare equal.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the comparison.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFCmpEQ(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    const bool result = runFloatCompare(fr,
                                        in,
                                        lhsSlot,
                                        rhsSlot,
                                        [](float lhs, float rhs) { return lhs == rhs; },
                                        [](double lhs, double rhs) { return lhs == rhs; });
    Slot out{};
    out.i64 = result ? 1 : 0;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fcmp.ne` opcode and record whether operands differ.
/// @details Materialises operands via @ref VMAccess::eval and forwards them to
///          @ref runFloatCompare, which selects @c float or @c double predicates
///          as needed.  NaN operands follow IEEE-754 semantics by yielding true
///          (unordered), and the boolean result is written back through
///          @ref ops::storeResult without affecting control-flow metadata.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the comparison.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFCmpNE(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    const bool result = runFloatCompare(fr,
                                        in,
                                        lhsSlot,
                                        rhsSlot,
                                        [](float lhs, float rhs) { return lhs != rhs; },
                                        [](double lhs, double rhs) { return lhs != rhs; });
    Slot out{};
    out.i64 = result ? 1 : 0;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fcmp.gt` opcode and record whether lhs > rhs.
/// @details Uses @ref ops::applyCompare so operands are evaluated once before
///          applying a strict greater-than check.  IEEE-754 semantics specify
///          that any NaN operand renders the comparison unordered, which this
///          handler treats as false (0).  Only the destination slot is mutated.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the comparison.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFCmpGT(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    const bool result = runFloatCompare(fr,
                                        in,
                                        lhsSlot,
                                        rhsSlot,
                                        [](float lhs, float rhs) { return lhs > rhs; },
                                        [](double lhs, double rhs) { return lhs > rhs; });
    Slot out{};
    out.i64 = result ? 1 : 0;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fcmp.lt` opcode and record whether lhs < rhs.
/// @details Leverages @ref ops::applyCompare to evaluate operands before
///          applying a strict less-than predicate.  Any NaN operand makes the
///          comparison unordered, which results in false (0).  Only the
///          destination slot is modified.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the comparison.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFCmpLT(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    const bool result = runFloatCompare(fr,
                                        in,
                                        lhsSlot,
                                        rhsSlot,
                                        [](float lhs, float rhs) { return lhs < rhs; },
                                        [](double lhs, double rhs) { return lhs < rhs; });
    Slot out{};
    out.i64 = result ? 1 : 0;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fcmp.le` opcode and record whether lhs <= rhs.
/// @details Relies on @ref ops::applyCompare to evaluate operands, then applies
///          a non-strict comparison.  Any NaN operand produces false (0) because
///          the comparison becomes unordered.  Signed zeros compare as equal,
///          matching IEEE-754 expectations.  Only the destination slot is
///          mutated.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the comparison.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFCmpLE(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    const bool result = runFloatCompare(fr,
                                        in,
                                        lhsSlot,
                                        rhsSlot,
                                        [](float lhs, float rhs) { return lhs <= rhs; },
                                        [](double lhs, double rhs) { return lhs <= rhs; });
    Slot out{};
    out.i64 = result ? 1 : 0;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fcmp.ge` opcode and record whether lhs >= rhs.
/// @details Utilises @ref ops::applyCompare to evaluate operands before applying
///          a non-strict greater-than predicate.  NaN operands produce false (0)
///          because the comparison becomes unordered.  Only the destination slot
///          is modified, leaving control-flow metadata untouched.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the comparison.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFCmpGE(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot lhsSlot = VMAccess::eval(vm, fr, in.operands[0]);
    Slot rhsSlot = VMAccess::eval(vm, fr, in.operands[1]);
    const bool result = runFloatCompare(fr,
                                        in,
                                        lhsSlot,
                                        rhsSlot,
                                        [](float lhs, float rhs) { return lhs >= rhs; },
                                        [](double lhs, double rhs) { return lhs >= rhs; });
    Slot out{};
    out.i64 = result ? 1 : 0;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `sitofp` opcode by converting a signed 64-bit integer to `double`.
/// @details Uses @ref ops::applyUnary to evaluate the operand slot and stores the
///          converted `double` value.  Host conversion semantics provide the
///          required IEEE-754 rounding behaviour; no additional traps are
///          raised because the IL spec allows the host to round out-of-range
///          integers.  Only the destination slot is mutated.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the conversion.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleSitofp(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    Slot out{};
    if (in.type.kind == Type::Kind::F32)
        out.f32 = static_cast<float>(value.i64);
    else
        out.f64 = static_cast<double>(value.i64);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `fptosi` opcode by converting a floating-point value to a
///        signed 64-bit integer.
/// @details Delegates to @ref ops::applyUnary to read the operand, then casts
///          the `double` to `int64_t` using truncation toward zero as mandated by
///          the IL spec.  The operation assumes the operand is representable; if
///          the host exhibits undefined behaviour for out-of-range values it is
///          preserved here because the opcode does not trap.  Only the
///          destination slot is modified.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the conversion.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer (unused).
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue.
VM::ExecResult handleFptosi(VM &vm,
                            Frame &fr,
                            const Instr &in,
                            const VM::BlockMap &blocks,
                            const BasicBlock *&bb,
                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    const double operand = operandAsF64(fr, in, 0, value);
    Slot out{};
    out.i64 = static_cast<int64_t>(operand);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `cast.fp_to_si.rte.chk` opcode with checked round-to-nearest conversion.
/// @details The workflow mirrors the IL specification:
///          1. Evaluate the operand via @ref VMAccess::eval.
///          2. Trap with @ref TrapKind::InvalidCast when the value is not finite.
///          3. Round to the nearest integer using @ref std::nearbyint (ties to even).
///          4. Trap with @ref TrapKind::Overflow when the rounded value falls
///             outside the signed 64-bit range or becomes non-finite.
///          5. Store the rounded result in the destination slot.
///          All traps flow through @ref RuntimeBridge so diagnostics include
///          instruction and block context.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the checked conversion.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer used for diagnostics.
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue when no trap fires.
VM::ExecResult handleCastFpToSiRteChk(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)ip;
    Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    const double operand = operandAsF64(fr, in, 0, value);
    if (!std::isfinite(operand))
    {
        RuntimeBridge::trap(TrapKind::InvalidCast,
                            "invalid fp operand in cast.fp_to_si.rte.chk",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
    }

    const double rounded = std::nearbyint(operand);
    if (!std::isfinite(rounded))
    {
        RuntimeBridge::trap(TrapKind::Overflow,
                            "fp overflow in cast.fp_to_si.rte.chk",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
    }

    constexpr double kMin = static_cast<double>(std::numeric_limits<int64_t>::min());
    constexpr double kMax = static_cast<double>(std::numeric_limits<int64_t>::max());
    if (rounded < kMin || rounded > kMax)
    {
        RuntimeBridge::trap(TrapKind::Overflow,
                            "fp overflow in cast.fp_to_si.rte.chk",
                            in.loc,
                            fr.func->name,
                            bb ? bb->label : "");
    }

    Slot out{};
    out.i64 = static_cast<int64_t>(rounded);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Execute the `cast.fp_to_ui.rte.chk` opcode with checked round-to-nearest conversion.
/// @details The helper delegates to @ref castFpToUiRoundedOrTrap to enforce the
///          conversion semantics: reject NaNs or negative inputs with
///          @ref TrapKind::InvalidCast, trap on overflow when the rounded value
///          exceeds the unsigned 64-bit range, and otherwise return the rounded
///          integer using banker’s rounding (ties to even).  The resulting value
///          is stored in the destination slot as a signed 64-bit integer so the
///          interpreter can continue processing.
/// @param vm Virtual machine coordinating execution (unused).
/// @param fr Active frame providing operand and destination slots.
/// @param in Instruction describing the checked conversion.
/// @param blocks Map of basic blocks for the current function (unused).
/// @param bb Reference to the current basic block pointer used for diagnostics.
/// @param ip Instruction index within @p bb (unused).
/// @return Execution result signalling the interpreter should continue when no trap fires.
VM::ExecResult handleCastFpToUiRteChk(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)ip;
    Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    const double operand = operandAsF64(fr, in, 0, value);
    const uint64_t rounded = castFpToUiRoundedOrTrap(operand, in, fr, bb);

    Slot out{};
    out.i64 = static_cast<int64_t>(rounded);
    ops::storeResult(fr, in, out);
    return {};
}

} // namespace il::vm::detail::floating
