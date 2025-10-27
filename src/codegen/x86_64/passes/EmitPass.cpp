//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/codegen/x86_64/passes/EmitPass.cpp
// Purpose: Implement the assembly emission pass for the x86-64 codegen pipeline.
// Key invariants: Emission only executes when register allocation has succeeded;
//                 diagnostics are surfaced through the shared Diagnostics sink.
// Ownership/Lifetime: Pass stores backend options by value; Module owns emitted
//                     artefacts.
// Links: docs/codemap.md, docs/architecture.md#codegen
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/EmitPass.hpp"

#include <string>
#include <utility>

namespace viper::codegen::x64::passes
{

/// @brief Construct the emit pass with backend configuration.
/// @param options Backend options controlling assembly emission.
EmitPass::EmitPass(CodegenOptions options) noexcept : options_(std::move(options)) {}

/// @brief Emit assembly for a lowered and allocated module.
/// @details Validates that prior stages completed, forwards errors from the
///          assembler, and persists the resulting artefacts on the module.
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
