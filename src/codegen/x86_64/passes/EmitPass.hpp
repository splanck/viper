//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/EmitPass.hpp
// Purpose: Declare the final emission pass for the x86-64 codegen pipeline.
// Key invariants: Emission requires register allocation to have marked completion.
// Ownership/Lifetime: Pass stores backend configuration by value and mutates Module state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes
{

/// \brief Emit assembly text for a lowered module using the backend facade.
class EmitPass final : public Pass
{
  public:
    /// @brief Construct the emission pass with the given codegen configuration.
    /// @param options Backend options controlling output format, debug info, etc.
    explicit EmitPass(CodegenOptions options) noexcept;

    /// @brief Run the emission pass: generate x86-64 assembly text from MIR.
    /// @param module The codegen module containing lowered and register-allocated MIR.
    /// @param diags Diagnostic sink for reporting emission errors.
    /// @return True if emission succeeded, false on error.
    bool run(Module &module, Diagnostics &diags) override;

  private:
    CodegenOptions options_;
};

} // namespace viper::codegen::x64::passes
