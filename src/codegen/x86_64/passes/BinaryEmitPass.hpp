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
//   - Populates Module::binaryTextSections and Module::binaryRodata CodeSections
//   - Does NOT produce assembly text (Module::codegenResult remains empty)
// Ownership/Lifetime:
//   - Pass stores backend options by value; borrows Module during run()
// Links: codegen/x86_64/passes/BinaryEmitPass.cpp,
//        codegen/x86_64/binenc/X64BinaryEncoder.hpp,
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
    /// @brief Construct the pass with the supplied codegen options.
    /// @details Options control output format (ELF/Mach-O/COFF) and debug
    ///          info presence. Stored by value so the pass can outlive the
    ///          caller's @ref CodegenOptions instance.
    /// @param options Backend configuration consumed during @ref run.
    explicit BinaryEmitPass(CodegenOptions options) noexcept;

    /// @brief Encode the module's MIR into machine code sections.
    /// @details Populates @c Module::binaryTextSections / @c binaryRodata
    ///          while leaving @c Module::codegenResult empty (text-assembly
    ///          emission is the @ref EmitPass alternative). @c binaryText is
    ///          populated only when debug line emission needs merged code.
    /// @param module Pipeline state mutated in place.
    /// @param diags Diagnostic sink.
    /// @return True on success.
    bool run(Module &module, Diagnostics &diags) override;

  private:
    CodegenOptions options_{}; ///< Backend configuration captured at construction.
};

} // namespace viper::codegen::x64::passes
