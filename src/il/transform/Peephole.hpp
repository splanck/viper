//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the peephole optimization pass for IL modules. Peephole
// optimization applies local pattern-based simplifications that replace instruction
// sequences with more efficient equivalent forms.
//
// Peephole optimization identifies small, localized patterns (instruction with
// specific constant operands) and replaces them with simpler forms. For example,
// adding zero to a value can be replaced with the value itself, multiplying by
// one is an identity operation, and shifts by zero are no-ops. This file defines
// a table-driven peephole framework with rules matching opcode + constant patterns.
//
// Key Responsibilities:
// - Match instructions against registered peephole patterns
// - Verify pattern preconditions (opcode, operand index, constant value)
// - Replace matched instructions with simpler equivalents (operand forwarding)
// - Support algebraic simplifications (identity, annihilation, commutativity)
// - Maintain SSA form and correct value uses
//
// Design Notes:
// The peephole system is table-driven using a compile-time rule registry (kRules).
// Each rule specifies a Match pattern (opcode, operand position, constant value)
// and a Replace action (which operand to forward as the result). The pass scans
// instructions linearly, applies matching rules, and updates uses. This design
// makes it easy to add new peephole rules without modifying the core pass logic.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

#include "il/core/Opcode.hpp"
#include "il/core/fwd.hpp"

namespace il::transform
{

/// \brief Pattern describing an instruction with a constant operand.
struct Match
{
    /// Opcode to match.
    core::Opcode op;
    /// Operand index holding the constant.
    unsigned constIdx;
    /// Required constant value.
    long long value;
};

/// \brief Replacement describing which operand to forward.
struct Replace
{
    /// Index of the operand to use as the replacement value.
    unsigned operandIdx;
};

/// \brief A peephole rule mapping a match to its replacement.
struct Rule
{
    Match match;  ///< Match pattern.
    Replace repl; ///< Replacement action.
};

/// \brief Registry of peephole rules.
inline constexpr std::array<Rule, 14> kRules{{
    {{core::Opcode::IAddOvf, 0, 0}, {1}},
    {{core::Opcode::IAddOvf, 1, 0}, {0}},
    {{core::Opcode::ISubOvf, 1, 0}, {0}},
    {{core::Opcode::IMulOvf, 0, 1}, {1}},
    {{core::Opcode::IMulOvf, 1, 1}, {0}},
    {{core::Opcode::And, 0, -1}, {1}},
    {{core::Opcode::And, 1, -1}, {0}},
    {{core::Opcode::Or, 0, 0}, {1}},
    {{core::Opcode::Or, 1, 0}, {0}},
    {{core::Opcode::Xor, 0, 0}, {1}},
    {{core::Opcode::Xor, 1, 0}, {0}},
    {{core::Opcode::Shl, 1, 0}, {0}},
    {{core::Opcode::LShr, 1, 0}, {0}},
    {{core::Opcode::AShr, 1, 0}, {0}},
}};

/// \brief Run peephole simplifications over @p m using registered rules.
void peephole(core::Module &m);

} // namespace il::transform
