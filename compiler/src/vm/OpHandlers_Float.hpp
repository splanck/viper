//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/OpHandlers_Float.hpp
// Purpose: Declare floating-point opcode handlers for the VM dispatcher.
// Key invariants: Handlers implement IEEE-754 semantics via host double operations.
// Ownership/Lifetime: Handlers only mutate the destination slot in the active frame.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Floating-point opcode handlers for the VM dispatcher.
/// @details Declares handlers for arithmetic, comparisons, and casts involving
///          IEEE-754 double values. Each handler evaluates operands from the
///          current frame, writes the destination slot, and updates the VM
///          instruction pointer as needed.

#pragma once

#include "vm/OpHandlerAccess.hpp"
#include "vm/VM.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

namespace il::vm::detail::floating
{
/// @brief Execute floating-point addition (FAdd).
/// @details Evaluates both operands as doubles, computes lhs + rhs using host
///          IEEE-754 semantics, and stores the result in the destination slot.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFAdd(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

/// @brief Execute floating-point subtraction (FSub).
/// @details Evaluates operands as doubles, computes lhs - rhs, and stores the
///          result in the destination slot.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFSub(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

/// @brief Execute floating-point multiplication (FMul).
/// @details Evaluates operands as doubles, computes lhs * rhs, and stores the
///          result in the destination slot.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFMul(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

/// @brief Execute floating-point division (FDiv).
/// @details Evaluates operands as doubles, computes lhs / rhs, and stores the
///          result in the destination slot. Division by zero follows IEEE-754
///          behavior (infinity or NaN) unless the IL semantics require traps.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFDiv(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

/// @brief Execute floating-point equality comparison (FCmpEQ).
/// @details Compares two doubles for equality using IEEE-754 semantics and
///          writes a boolean result into the destination slot.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFCmpEQ(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Execute floating-point inequality comparison (FCmpNE).
/// @details Compares two doubles for inequality using IEEE-754 semantics and
///          writes a boolean result into the destination slot.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFCmpNE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Execute floating-point greater-than comparison (FCmpGT).
/// @details Compares two doubles (lhs > rhs) and writes a boolean result.
///          IEEE-754 NaN comparisons follow ordered comparison rules.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFCmpGT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Execute floating-point less-than comparison (FCmpLT).
/// @details Compares two doubles (lhs < rhs) and writes a boolean result.
///          IEEE-754 NaN comparisons follow ordered comparison rules.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFCmpLT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Execute floating-point less-or-equal comparison (FCmpLE).
/// @details Compares two doubles (lhs <= rhs) and writes a boolean result.
///          IEEE-754 NaN comparisons follow ordered comparison rules.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFCmpLE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Execute floating-point greater-or-equal comparison (FCmpGE).
/// @details Compares two doubles (lhs >= rhs) and writes a boolean result.
///          IEEE-754 NaN comparisons follow ordered comparison rules.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFCmpGE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Execute signed integer to floating-point conversion (SiToFp).
/// @details Converts a signed integer operand to double and writes the result.
///          The conversion follows host IEEE-754 semantics.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleSitofp(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Execute floating-point to signed integer conversion (FpToSi).
/// @details Converts a double to a signed integer using the IL conversion
///          semantics (typically truncation). Out-of-range handling is defined
///          by the opcode semantics; checked variants trap explicitly.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleFptosi(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

/// @brief Execute checked float-to-signed-int conversion with range traps.
/// @details Validates that the double operand is within the target integer
///          range before converting. If the value is out of range or NaN, the
///          handler emits a trap per IL semantics.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleCastFpToSiRteChk(VM &vm,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip);

/// @brief Execute checked float-to-unsigned-int conversion with range traps.
/// @details Validates that the double operand is within the unsigned target
///          range before converting. If the value is out of range or NaN, the
///          handler emits a trap per IL semantics.
/// @param vm Active VM instance.
/// @param fr Current execution frame.
/// @param in Instruction being executed.
/// @param blocks Block map for the current function.
/// @param bb In/out current basic block pointer.
/// @param ip In/out instruction pointer within @p bb.
/// @return Execution result indicating continue, trap, or control transfer.
VM::ExecResult handleCastFpToUiRteChk(VM &vm,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip);

} // namespace il::vm::detail::floating
