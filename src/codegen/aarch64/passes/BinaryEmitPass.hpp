//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/BinaryEmitPass.hpp
// Purpose: Declare the binary emission pass for the AArch64 codegen pipeline.
//          Encodes MIR into machine code bytes via A64BinaryEncoder.
// Key invariants:
//   - Requires register allocation to have completed
//   - Populates AArch64Module::binaryText and AArch64Module::binaryRodata
// Ownership/Lifetime:
//   - Stateless pass; mutates AArch64Module binary fields
// Links: codegen/aarch64/binenc/A64BinaryEncoder.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace viper::codegen::aarch64::passes
{

/// @brief Encode all MIR functions into machine code bytes.
class BinaryEmitPass final : public Pass
{
  public:
    /// @brief Run the binary emission pass.
    /// @param module Module state; mir must have physical registers assigned.
    /// @param diags  Diagnostic sink for emission errors.
    /// @return True if emission succeeded for all functions.
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::aarch64::passes
