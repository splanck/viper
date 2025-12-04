//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ResultTypeChecker helper class, which validates that an
// instruction's result (if any) matches the requirements specified by its opcode
// metadata. This is part of the structural verification phase that precedes
// semantic type checking.
//
// IL instructions have varying result requirements: some produce no result (void),
// some must produce exactly one result, and the result must match the type category
// declared in the opcode metadata. The ResultTypeChecker encapsulates the logic
// for validating these result constraints using the InstructionSpec from SpecTables.
//
// Key Responsibilities:
// - Verify instructions with no result (ResultArity::Zero) have no result ID
// - Ensure instructions requiring a result (ResultArity::One) declare a result
// - Validate the result type matches or is compatible with metadata requirements
// - Generate precise error messages for result presence/type mismatches
//
// Design Notes:
// ResultTypeChecker is a stateless helper constructed with the verification context
// and spec, then immediately executed via run(). This design pattern (construct and
// execute) provides a focused API while keeping validation logic grouped with its
// data dependencies. The checker is in il::verify::detail as it's an internal
// component of the table-driven verification system.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/verify/SpecTables.hpp"
#include "il/verify/VerifyCtx.hpp"

#include "support/diag_expected.hpp"

#include <string_view>

namespace il::verify::detail
{

/// @brief Ensures an instruction's result matches opcode metadata expectations.
class ResultTypeChecker
{
  public:
    ResultTypeChecker(const VerifyCtx &ctx, const InstructionSpec &spec);

    /// @brief Validates the presence and type of the instruction result.
    /// @return Empty on success or a populated diagnostic on failure.
    [[nodiscard]] il::support::Expected<void> run() const;

  private:
    il::support::Expected<void> report(std::string_view message) const;

    const VerifyCtx &ctx_;
    const InstructionSpec &spec_;
};

} // namespace il::verify::detail
