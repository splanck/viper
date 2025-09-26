// File: src/il/transform/Peephole.hpp
// Purpose: Declares a peephole optimizer for IL modules.
// Key invariants: Applies local simplifications preserving semantics.
// Ownership/Lifetime: Operates in place on the provided module.
// Links: docs/codemap.md
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
