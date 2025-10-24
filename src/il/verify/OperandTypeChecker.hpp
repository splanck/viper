// File: src/il/verify/OperandTypeChecker.hpp
// Purpose: Declares a helper that validates operand types against opcode metadata.
// Key invariants: Operates on the verification context for a single instruction.
// Ownership/Lifetime: Non-owning references to verification data structures.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/OpcodeInfo.hpp"
#include "il/verify/VerifyCtx.hpp"

#include "support/diag_expected.hpp"

#include <string_view>

namespace il::verify::detail
{

/// @brief Ensures an instruction's operands satisfy the metadata type requirements.
class OperandTypeChecker
{
  public:
    OperandTypeChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info);

    /// @brief Validates operand types described by opcode metadata.
    /// @return Empty on success or a populated diagnostic on failure.
    il::support::Expected<void> run() const;

  private:
    il::support::Expected<void> report(std::string_view message) const;

    const VerifyCtx &ctx_;
    const il::core::OpcodeInfo &info_;
};

} // namespace il::verify::detail
