//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/EmitPass.cpp
// Purpose: Implement the assembly emission pass for the x86-64 codegen pipeline.
//          Converts lowered and register-allocated MIR into textual assembly text.
// Key invariants:
//   - Pass verifies upstream stages completed before invoking the backend emitter.
//   - Diagnostics are surfaced through the shared Diagnostics sink.
// Ownership/Lifetime:
//   - Pass owns CodegenOptions by value; borrows Module for the duration of run().
// Links: codegen/x86_64/passes/EmitPass.hpp,
//        codegen/x86_64/Backend.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/EmitPass.hpp"

#include <string>
#include <utility>

namespace viper::codegen::x64::passes {

/// @brief Construct the emit pass with backend configuration.
/// @details Stores @p options by value so the pass can outlive the caller's
///          configuration object.  The stored options are forwarded to the
///          backend emitter during @ref run.
/// @param options Backend options controlling assembly emission.
EmitPass::EmitPass(CodegenOptions options) noexcept : options_(std::move(options)) {}

/// @brief Emit assembly for a lowered and allocated module.
/// @details Performs a series of preconditions before calling the backend
///          emitter:
///          1. Checks that register allocation populated @p module.registersAllocated.
///          2. Ensures MIR, frame, and target artefacts are present.
///          3. Invokes @ref emitMIRToAssembly and surfaces any backend errors
///             through @p diags.
///          When emission succeeds the resulting @ref CodegenResult is stored on
///          the module so later passes (or CLI drivers) can access the assembly
///          text and object code paths without repeating the expensive work.
/// @param module Code generation module containing lowering outputs.
/// @param diags Diagnostics sink that records emission failures.
/// @return True when assembly emission succeeds without diagnostics.
bool EmitPass::run(Module &module, Diagnostics &diags) {
    if (!module.registersAllocated) {
        diags.error("emit: register allocation has not completed");
        return false;
    }
    if (module.target == nullptr) {
        diags.error("emit: target selection is missing prior to emission");
        return false;
    }
    if (module.mir.size() != module.frames.size()) {
        diags.error("emit: MIR/frame state is inconsistent");
        return false;
    }

    CodegenResult result = emitMIRToAssembly(module.mir, module.roData, *module.target, options_);
    if (!result.errors.empty()) {
        std::string message = "error: x64 codegen failed:\n";
        message += result.errors;
        message.push_back('\n');
        diags.error(std::move(message));
        return false;
    }

    module.codegenResult = std::move(result);
    return true;
}

} // namespace viper::codegen::x64::passes
