//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/Noreturn.hpp
// Purpose: Shared predicates that classify a direct call to a runtime helper
//          that traps or terminates (never returns to its caller). Passes use
//          this to model such calls as no-return CFG exits so they can prune
//          dead instruction tails and avoid emitting unreachable epilogues.
// Key invariants:
//   - Only DIRECT calls are recognized: the instruction must be MOpcode::Bl
//     with a Label first operand. Indirect/register branches are never
//     classified as no-return here.
//   - The label is resolved through il::runtime::mapCanonicalRuntimeName
//     first, so platform-mangled or aliased names match the canonical set.
//   - The recognized set (rt_trap*, rt_arr_oob_panic, rt_trap_div0/ovf, ...)
//     must stay in sync with the runtime helpers that genuinely do not
//     return; adding a returning symbol here would prune live code.
// Ownership/Lifetime:
//   - Stateless inline predicates; no allocation, no retained state. The
//     MInstr / MBasicBlock arguments are caller-owned and read-only.
// Links: codegen/aarch64/MachineIR.hpp,
//        il/runtime/RuntimeNameMap.hpp,
//        codegen/aarch64/passes/LegalizePass.cpp (dead-tail pruning caller)
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"
#include "il/runtime/RuntimeNameMap.hpp"

#include <string_view>

namespace viper::codegen::aarch64 {

/// @brief Return true if @p symbol names a runtime helper that traps or terminates.
[[nodiscard]] inline bool isNoReturnRuntimeSymbol(std::string_view symbol) noexcept {
    return symbol == "rt_trap_ovf" || symbol == "rt_trap_div0" || symbol == "rt_trap_raise_error" ||
           symbol == "rt_trap_string" || symbol == "rt_arr_oob_panic" || symbol == "rt_trap";
}

/// @brief Return true if @p instr is a direct call to a known no-return runtime helper.
[[nodiscard]] inline bool isNoReturnCall(const MInstr &instr) {
    if (instr.opc != MOpcode::Bl || instr.ops.empty() || instr.ops[0].kind != MOperand::Kind::Label)
        return false;

    const std::string &raw = instr.ops[0].label;
    if (auto mapped = il::runtime::mapCanonicalRuntimeName(raw))
        return isNoReturnRuntimeSymbol(*mapped);
    return isNoReturnRuntimeSymbol(raw);
}

/// @brief Return true if @p block ends in a known no-return runtime call.
[[nodiscard]] inline bool endsInNoReturnCall(const MBasicBlock &block) {
    return !block.instrs.empty() && isNoReturnCall(block.instrs.back());
}

} // namespace viper::codegen::aarch64
