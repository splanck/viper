// File: src/il/verify/Rule.hpp
// Purpose: Declares the instruction verification rule interface.
// Key invariants: Rules operate with verifier-provided context and do not own data.
// Ownership/Lifetime: Implementations hold references managed by the verifier.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Instr.hpp"

namespace il::verify
{

/// @brief Interface implemented by verification rules for specific opcodes.
class Rule
{
  public:
    virtual ~Rule() = default;

    /// @brief Validate the given instruction using the rule context.
    /// @param instr Instruction to be validated.
    /// @return True when the instruction satisfies the rule.
    virtual bool check(const il::core::Instr &instr) = 0;
};

} // namespace il::verify
