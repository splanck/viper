// src/codegen/x86_64/LoweringRules.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Declare the rule registry that drives IL to MIR lowering.
// Invariants: Rules are stateless function pairs matched deterministically.
// Ownership: Registry stored as process-wide static vector, returned by ref.
// Notes: Rules operate on MIRBuilder to emit Machine IR sequences.

#pragma once

#include <vector>

namespace viper::codegen::x64
{
struct ILInstr;
} // namespace viper::codegen::x64

namespace IL
{
using Instr = viper::codegen::x64::ILInstr;
} // namespace IL

namespace viper::codegen::x64
{

class MIRBuilder;

/// \brief Description of a single IL lowering rule.
struct LoweringRule
{
    bool (*match)(const IL::Instr &); ///< Match predicate invoked before emit.
    void (*emit)(const IL::Instr &, MIRBuilder &); ///< Emit routine for matched opcode.
    const char *name; ///< Debug name describing the handled opcode family.
};

/// \brief Retrieve the global registry of lowering rules.
[[nodiscard]] const std::vector<LoweringRule> &viper_get_lowering_rules();

/// \brief Select the first rule whose match predicate accepts the instruction.
[[nodiscard]] const LoweringRule *viper_select_rule(const IL::Instr &instr);

} // namespace viper::codegen::x64
