//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/OperandRoles.hpp
// Purpose: Shared x86-64 Machine IR operand role classification.
// Key invariants:
//   - operandRoles() covers all defined MIR opcodes deterministically.
//   - EFlags queries are consistent with the x86-64 instruction set.
// Ownership/Lifetime:
//   - Stateless free functions; no dynamic allocation or persistent state.
// Links: codegen/x86_64/OperandRoles.cpp,
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/MachineIR.hpp"

#include <cstddef>
#include <utility>

namespace zanna::codegen::x64 {

/// @brief Return `{isUse, isDef}` for an operand of an x86-64 MIR instruction.
/// @details Used by liveness, DCE, and the register allocator to classify every
///          operand of every MIR opcode without re-encoding the rules per pass.
/// @param instr Machine instruction whose operand is being classified.
/// @param idx   Zero-based index into `instr.ops`.
/// @return Pair indicating whether the operand is read, written, or both.
[[nodiscard]] std::pair<bool, bool> operandRoles(const MInstr &instr, std::size_t idx) noexcept;

/// @brief Test whether @p opcode reads the x86 EFLAGS register.
/// @details EFLAGS-reading instructions include the conditional branches, `SETcc`,
///          `CMOVcc`, and `ADC`/`SBB`. Used by fold-safety checks to refuse to
///          delete a flag-producing instruction whose flags are consumed later.
/// @param opcode Opcode to classify.
/// @return True if @p opcode reads EFLAGS.
[[nodiscard]] bool usesEFlags(MOpcode opcode) noexcept;

/// @brief Test whether @p opcode writes the x86 EFLAGS register.
/// @details Flag-writing instructions include all arithmetic/logical forms
///          (`ADD`, `SUB`, `AND`, etc.), the compare family, and the test family.
///          Used by liveness to model flags as an implicit def.
/// @param opcode Opcode to classify.
/// @return True if @p opcode writes EFLAGS.
[[nodiscard]] bool definesEFlags(MOpcode opcode) noexcept;

/// @brief Test whether @p opcode has observable side effects (memory or otherwise).
/// @details Returns true for stores, calls, traps, and any opcode that escapes the
///          virtual-register model. DCE consults this predicate to refuse to delete
///          such instructions even when their result is unused.
/// @param opcode Opcode to classify.
/// @return True if @p opcode has observable side effects.
[[nodiscard]] bool hasObservableSideEffects(MOpcode opcode) noexcept;

} // namespace zanna::codegen::x64
