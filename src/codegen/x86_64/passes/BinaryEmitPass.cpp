//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/BinaryEmitPass.cpp
// Purpose: Implement the binary emission pass that encodes MIR into machine
//          code bytes via the Backend::emitModuleToBinary entry point.
// Key invariants:
//   - Requires register allocation to have completed
//   - Populates Module::binaryText and Module::binaryRodata
// Ownership/Lifetime:
//   - Pass borrows Module during run(), does not own any state beyond options_
// Links: codegen/x86_64/passes/BinaryEmitPass.hpp,
//        codegen/x86_64/Backend.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/BinaryEmitPass.hpp"

#include <string>
#include <utility>

namespace viper::codegen::x64::passes {

/// @brief Construct the binary emit pass with backend configuration.
/// @details Stores @p options by value so the pass survives the caller's local
///          options object. Used to drive @ref emitModuleToBinary later.
BinaryEmitPass::BinaryEmitPass(CodegenOptions options) noexcept : options_(std::move(options)) {}

/// @brief Emit raw machine code for a lowered and allocated module.
/// @details Mirrors @ref EmitPass::run but produces binary CodeSections instead
///          of assembly text. Validates that register allocation completed,
///          target/frame state are consistent, then forwards to the backend's
///          @ref emitModuleToBinary which fills @p module.binaryText and
///          @p module.binaryRodata.
/// @return true on success; otherwise records errors via @p diags.
bool BinaryEmitPass::run(Module &module, Diagnostics &diags) {
    if (!module.registersAllocated) {
        diags.error("binary emit: register allocation has not completed");
        return false;
    }
    if (module.target == nullptr) {
        diags.error("binary emit: target selection is missing prior to emission");
        return false;
    }
    if (module.mir.size() != module.frames.size()) {
        diags.error("binary emit: MIR/frame state is inconsistent");
        return false;
    }

    BinaryEmitResult result =
        emitMIRToBinary(module.mir, module.frames, module.roData, *module.target, options_);
    if (!result.errors.empty()) {
        std::string message = "error: x64 binary codegen failed:\n";
        message += result.errors;
        message.push_back('\n');
        diags.error(std::move(message));
        return false;
    }

    module.binaryText = std::move(result.text);
    module.binaryRodata = std::move(result.rodata);
    module.binaryTextSections = std::move(result.textSections);
    module.debugLineData = std::move(result.debugLineData);
    return true;
}

} // namespace viper::codegen::x64::passes
