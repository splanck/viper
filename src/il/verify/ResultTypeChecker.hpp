// File: src/il/verify/ResultTypeChecker.hpp
// Purpose: Declares a helper that validates result presence and types against opcode metadata.
// Key invariants: Operates on the verification context for a single instruction.
// Ownership/Lifetime: Non-owning references to verification data structures.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/verify/VerifyCtx.hpp"
#include "il/verify/SpecTables.hpp"

#include "support/diag_expected.hpp"

#include <string_view>

namespace il::verify::detail
{

/// @brief Ensures an instruction's result matches opcode metadata expectations.
class ResultTypeChecker
{
  public:
    ResultTypeChecker(const VerifyCtx &ctx, const spec::InstructionSpec &spec);

    /// @brief Validates the presence and type of the instruction result.
    /// @return Empty on success or a populated diagnostic on failure.
    il::support::Expected<void> run() const;

  private:
    il::support::Expected<void> report(std::string_view message) const;

    const VerifyCtx &ctx_;
    const spec::InstructionSpec &spec_;
};

} // namespace il::verify::detail
