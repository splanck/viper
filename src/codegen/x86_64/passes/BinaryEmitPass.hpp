//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/BinaryEmitPass.hpp
// Purpose: Declare the binary emission pass for the x86-64 codegen pipeline.
//          Encodes MIR into machine code bytes via X64BinaryEncoder instead of
//          emitting text assembly.
// Key invariants:
//   - Requires register allocation to have completed before running
//   - Populates Module::binaryText and Module::binaryRodata CodeSections
//   - Does NOT produce assembly text (Module::codegenResult remains empty)
// Ownership/Lifetime:
//   - Pass stores isDarwin flag by value; borrows Module during run()
// Links: codegen/x86_64/binenc/X64BinaryEncoder.hpp
//        codegen/x86_64/passes/PassManager.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes {

/// \brief Encode MIR functions into machine code bytes for the x86-64 backend.
class BinaryEmitPass final : public Pass {
  public:
    /// @param isDarwin If true, symbol names get underscore-prefixed (Mach-O convention).
    BinaryEmitPass(bool isDarwin, CodegenOptions options) noexcept;

    bool run(Module &module, Diagnostics &diags) override;

  private:
    bool isDarwin_;
    CodegenOptions options_{};
};

} // namespace viper::codegen::x64::passes
