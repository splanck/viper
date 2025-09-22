// File: src/vm/OpHandlerUtils.hpp
// Purpose: Shared helper routines for VM opcode handlers.
// Key invariants: Helpers operate on VM frames without leaking references.
// Ownership/Lifetime: Functions mutate frame state in-place without storing globals.
// Links: docs/il-spec.md
#pragma once

#include "vm/VM.hpp"

#include "il/core/Instr.hpp"

#include <utility>

namespace il::vm::detail
{
namespace ops
{
/// @brief Store the result of an instruction if it produces one.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param val Slot to write into the destination register.
void storeResult(Frame &fr, const il::core::Instr &in, const Slot &val);

/// @brief Internal dispatcher that evaluates operands via the VM.
struct OperandDispatcher
{
    template <typename Compute>
    static VM::ExecResult runBinary(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    Compute &&compute)
    {
        Slot lhs = vm.eval(fr, in.operands[0]);
        Slot rhs = vm.eval(fr, in.operands[1]);
        Slot out{};
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
        Slot lhs = vm.eval(fr, in.operands[0]);
        Slot rhs = vm.eval(fr, in.operands[1]);
        Slot out{};
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
VM::ExecResult applyBinary(VM &vm,
                           Frame &fr,
                           const il::core::Instr &in,
                           Compute &&compute)
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
VM::ExecResult applyCompare(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            Compare &&compare)
{
    return OperandDispatcher::runCompare(vm, fr, in, std::forward<Compare>(compare));
}
} // namespace ops
} // namespace il::vm::detail

