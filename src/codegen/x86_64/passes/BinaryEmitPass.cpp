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
//   - Pass borrows Module during run(), does not own any state beyond isDarwin_
// Links: codegen/x86_64/Backend.hpp (emitModuleToBinary)
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/BinaryEmitPass.hpp"

#include <string>
#include <utility>

namespace viper::codegen::x64::passes {

BinaryEmitPass::BinaryEmitPass(bool isDarwin, CodegenOptions options) noexcept
    : isDarwin_(isDarwin), options_(std::move(options)) {}

bool BinaryEmitPass::run(Module &module, Diagnostics &diags) {
    if (!module.registersAllocated) {
        diags.error("binary emit: register allocation has not completed");
        return false;
    }
    if (!module.lowered) {
        diags.error("binary emit: lowering artefact missing prior to emission");
        return false;
    }

    BinaryEmitResult result = emitModuleToBinary(*module.lowered, options_, isDarwin_);
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
