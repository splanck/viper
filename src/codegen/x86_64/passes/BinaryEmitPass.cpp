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
//   - Populates Module::binaryTextSections and Module::binaryRodata
// Ownership/Lifetime:
//   - Pass borrows Module during run(), does not own any state beyond options_
// Links: codegen/x86_64/passes/BinaryEmitPass.hpp,
//        codegen/x86_64/Backend.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/BinaryEmitPass.hpp"

#include "codegen/common/ScalarGlobalLayout.hpp"
#include "codegen/common/objfile/CodeSection.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace viper::codegen::x64::passes {

namespace {

/// @brief Build the writable __data/.data section from a module's scalar globals.
/// @details Mutable non-string globals (`global i64 @counter = 41`) become defined
///          Global symbols carrying little-endian initializer bytes. The text
///          section's undefined references to these names coalesce onto these
///          definitions in the object writer. Layout rules are shared with the
///          AArch64 path via @ref common::scalarGlobalLayout.
objfile::CodeSection buildScalarDataSection(const il::core::Module &mod) {
    objfile::CodeSection data;
    for (const auto &g : mod.globals) {
        const auto layout = common::scalarGlobalLayout(g.type.kind);
        if (layout.sizeBytes == 0)
            continue; // strings (rodata) / void / error / resumetok — nothing to emit
        const uint64_t raw = common::scalarGlobalRawBits(g.init, layout.isFloat);
        data.alignTo(static_cast<size_t>(layout.sizeBytes));
        data.defineSymbol(g.name, objfile::SymbolBinding::Global, objfile::SymbolSection::Data);
        for (int i = 0; i < layout.sizeBytes; ++i)
            data.emit8(static_cast<uint8_t>((raw >> (8 * i)) & 0xFF));
    }
    return data;
}

} // namespace

/// @brief Construct the binary emit pass with backend configuration.
/// @details Stores @p options by value so the pass survives the caller's local
///          options object. Used to drive @ref emitModuleToBinary later.
BinaryEmitPass::BinaryEmitPass(CodegenOptions options) noexcept : options_(std::move(options)) {}

/// @brief Emit raw machine code for a lowered and allocated module.
/// @details Mirrors @ref EmitPass::run but produces binary CodeSections instead
///          of assembly text. Validates that register allocation completed,
///          target/frame state are consistent, then forwards to the backend's
///          @ref emitMIRToBinary which fills @p module.binaryTextSections and
///          @p module.binaryRodata. A merged @c binaryText is produced only
///          when debug line emission needs one contiguous address space.
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

    if (!result.text.empty() || module.mir.empty() || !result.debugLineData.empty())
        module.binaryText = std::move(result.text);
    else
        module.binaryText.reset();
    module.binaryRodata = std::move(result.rodata);
    module.binaryData = buildScalarDataSection(module.il);
    module.binaryTextSections = std::move(result.textSections);
    module.debugLineData = std::move(result.debugLineData);
    return true;
}

} // namespace viper::codegen::x64::passes
