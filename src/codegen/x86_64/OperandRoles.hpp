//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

namespace viper::codegen::x64 {

/// @brief Return {isUse, isDef} for an operand of an x86-64 MIR instruction.
[[nodiscard]] std::pair<bool, bool> operandRoles(const MInstr &instr, std::size_t idx) noexcept;

/// @brief Whether the instruction reads x86 EFLAGS.
[[nodiscard]] bool usesEFlags(MOpcode opcode) noexcept;

/// @brief Whether the instruction writes x86 EFLAGS.
[[nodiscard]] bool definesEFlags(MOpcode opcode) noexcept;

/// @brief Whether the instruction may read/write memory or otherwise has effects.
[[nodiscard]] bool hasObservableSideEffects(MOpcode opcode) noexcept;

} // namespace viper::codegen::x64
