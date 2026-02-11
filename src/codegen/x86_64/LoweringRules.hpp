//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/LoweringRules.hpp
// Purpose: Declare the rule registry that drives IL to MIR lowering.
// Key invariants: Rules are initialised lazily on first access and remain
//                 immutable thereafter; rule selection proceeds in registration
//                 order until a match predicate succeeds.
// Ownership/Lifetime: Registry stored as process-wide static vector, returned by ref.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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
    bool (*match)(const IL::Instr &);              ///< Match predicate invoked before emit.
    void (*emit)(const IL::Instr &, MIRBuilder &); ///< Emit routine for matched opcode.
    const char *name; ///< Debug name describing the handled opcode family.
};

/// \brief Retrieve the global registry of lowering rules.
///
/// @details The registry is lazily initialised on the first call and remains
///          immutable for the lifetime of the process. Rules are stored in
///          registration order, which determines match priority.
///
/// @return Const reference to the process-wide vector of lowering rules.
[[nodiscard]] const std::vector<LoweringRule> &viper_get_lowering_rules();

/// \brief Select the first rule whose match predicate accepts the instruction.
///
/// @details Iterates the rule registry in registration order, invoking each
///          rule's match predicate until one returns true. Returns nullptr
///          when no rule matches.
///
/// @param instr The IL instruction to match against registered rules.
/// @return Pointer to the first matching LoweringRule, or nullptr if none match.
[[nodiscard]] const LoweringRule *viper_select_rule(const IL::Instr &instr);

} // namespace viper::codegen::x64
