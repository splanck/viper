//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Rule interface, which represents a verifiable constraint
// on IL instructions. Rules provide a pluggable mechanism for extending the
// verifier with custom validation logic beyond the standard type and structural
// checks.
//
// The Rule abstraction exists to support future extensibility where specialized
// verification constraints might be registered dynamically or configured based on
// verification modes. Currently the verifier uses built-in strategies rather than
// dynamic rule registration, but the Rule interface establishes the pattern for
// instruction-level validation predicates.
//
// Key Responsibilities:
// - Define the interface for instruction-level verification predicates
// - Enable polymorphic verification logic through virtual check() method
// - Support future extensibility for custom verification rules
//
// Design Notes:
// The Rule interface follows the classic strategy pattern with a boolean return
// convention. Implementations are expected to emit diagnostics through some
// external mechanism (typically a DiagSink) rather than returning error details,
// simplifying the interface at the cost of requiring side effects for error
// reporting. This matches the legacy verifier architecture and may evolve toward
// Expected<void> returns in future refactoring.
//
//===----------------------------------------------------------------------===//

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
