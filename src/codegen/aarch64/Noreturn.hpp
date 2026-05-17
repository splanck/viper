//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/Noreturn.hpp
// Purpose: Shared classification of direct runtime calls that do not return.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"
#include "il/runtime/RuntimeNameMap.hpp"

#include <string_view>

namespace viper::codegen::aarch64 {

/// @brief Return true if @p symbol names a runtime helper that traps or terminates.
[[nodiscard]] inline bool isNoReturnRuntimeSymbol(std::string_view symbol) noexcept {
    return symbol == "rt_trap_ovf" || symbol == "rt_trap_div0" ||
           symbol == "rt_trap_raise_error" || symbol == "rt_trap_string" ||
           symbol == "rt_arr_oob_panic" || symbol == "rt_trap";
}

/// @brief Return true if @p instr is a direct call to a known no-return runtime helper.
[[nodiscard]] inline bool isNoReturnCall(const MInstr &instr) {
    if (instr.opc != MOpcode::Bl || instr.ops.empty() ||
        instr.ops[0].kind != MOperand::Kind::Label)
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
