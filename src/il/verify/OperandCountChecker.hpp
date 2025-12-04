//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the OperandCountChecker helper class, which validates that
// an instruction provides the correct number of operands as specified by its opcode
// metadata. This is a structural verification check performed before type checking.
//
// IL opcodes have varying operand requirements: fixed arity (exactly N operands),
// variadic (any number of operands meeting some minimum), or ranged (between min
// and max operands). The OperandCountChecker encapsulates the logic for validating
// these constraints using the InstructionSpec from SpecTables.
//
// Key Responsibilities:
// - Verify fixed-arity instructions provide exactly the required operand count
// - Validate variadic instructions meet minimum operand requirements
// - Ensure ranged-arity instructions fall within the specified bounds
// - Generate precise error messages identifying count mismatches
//
// Design Notes:
// OperandCountChecker follows the construct-and-execute pattern used throughout
// the detail verification helpers. It's initialized with the verification context
// and opcode spec, then immediately executed via run(). This keeps the checker
// stateless and focused on a single validation concern. The checker is in the
// il::verify::detail namespace as an internal component of table-driven verification.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/verify/SpecTables.hpp"
#include "il/verify/VerifyCtx.hpp"

#include "support/diag_expected.hpp"

#include <string_view>

namespace il::verify::detail
{

/// @brief Ensures an instruction provides the expected number of operands.
class OperandCountChecker
{
  public:
    OperandCountChecker(const VerifyCtx &ctx, const InstructionSpec &spec);

    /// @brief Validates the operand count described by the instruction and metadata.
    /// @return Empty on success or a populated diagnostic on failure.
    [[nodiscard]] il::support::Expected<void> run() const;

  private:
    il::support::Expected<void> report(std::string_view message) const;

    const VerifyCtx &ctx_;
    const InstructionSpec &spec_;
};

} // namespace il::verify::detail
