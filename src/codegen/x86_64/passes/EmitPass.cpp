//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the code-generation pass that converts lowered and allocated
// machine IR into textual x86-64 assembly.  The pass verifies upstream stages
// completed successfully before invoking the backend emitter and records any
// diagnostic output through the shared diagnostics sink.  Persisting the emission
// artefacts on the module keeps downstream tooling agnostic of the pass order
// used to produce them.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Assembly emission pass for the x86-64 backend.
/// @details Binds together the register allocation results, backend emission
///          options, and diagnostics reporting so CLI tools and higher-level
///          pipelines can trigger assembly generation through a single entry
///          point.

#include "codegen/x86_64/passes/EmitPass.hpp"

#include <string>
#include <utility>

namespace viper::codegen::x64::passes
{

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
///          2. Ensures lowering artefacts are present.
///          3. Invokes @ref emitModuleToAssembly and surfaces any backend errors
///             through @p diags.
///          When emission succeeds the resulting @ref CodegenResult is stored on
///          the module so later passes (or CLI drivers) can access the assembly
///          text and object code paths without repeating the expensive work.
/// @param module Code generation module containing lowering outputs.
/// @param diags Diagnostics sink that records emission failures.
/// @return True when assembly emission succeeds without diagnostics.
bool EmitPass::run(Module &module, Diagnostics &diags)
{
    if (!module.registersAllocated)
    {
        diags.error("emit: register allocation has not completed");
        return false;
    }
    if (!module.lowered)
    {
        diags.error("emit: lowering artefact missing prior to emission");
        return false;
    }

    CodegenResult result = emitModuleToAssembly(*module.lowered, options_);
    if (!result.errors.empty())
    {
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
