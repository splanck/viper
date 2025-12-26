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
    explicit EmitPass(CodegenOptions options) noexcept;

    bool run(Module &module, Diagnostics &diags) override;

  private:
    CodegenOptions options_;
};

} // namespace viper::codegen::x64::passes
