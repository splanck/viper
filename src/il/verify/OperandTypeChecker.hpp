//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the OperandTypeChecker helper class, which validates that
// instruction operands have types compatible with the requirements specified by
// opcode metadata. This is the type-checking layer of structural verification.
//
// IL opcodes declare type category constraints for their operands: some require
// specific concrete types (i32, ptr, etc.), others require categories that match
// the instruction's declared type, and some accept any type. The OperandTypeChecker
// enforces these constraints using the InstructionSpec metadata and the type
// environment maintained during verification.
//
// Key Responsibilities:
// - Resolve actual operand types from literals, temporaries, and block parameters
// - Match operand types against metadata type category requirements
// - Handle InstrType category (operand type must match instruction's result type)
// - Validate literal values fit within their declared type constraints
// - Generate precise type mismatch diagnostics
//
// Design Notes:
// OperandTypeChecker is the most complex of the structural verification helpers
// because it must coordinate between the type environment (TypeInference), opcode
// metadata (InstructionSpec), and actual operand values. It follows the same
// construct-and-execute pattern as other detail checkers, with the run() method
// orchestrating per-operand type validation. The checker is internal to the
// table-driven verification system.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/verify/SpecTables.hpp"
#include "il/verify/VerifyCtx.hpp"

#include "support/diag_expected.hpp"

#include <string_view>

namespace il::verify::detail
{

/// @brief Ensures an instruction's operands satisfy the metadata type requirements.
class OperandTypeChecker
{
  public:
    OperandTypeChecker(const VerifyCtx &ctx, const InstructionSpec &spec);

    /// @brief Validates operand types described by opcode metadata.
    /// @return Empty on success or a populated diagnostic on failure.
    [[nodiscard]] il::support::Expected<void> run() const;

  private:
    il::support::Expected<void> report(std::string_view message) const;

    const VerifyCtx &ctx_;
    const InstructionSpec &spec_;
};

} // namespace il::verify::detail
