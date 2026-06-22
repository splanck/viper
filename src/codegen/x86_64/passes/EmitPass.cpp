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

#include "codegen/common/ScalarGlobalLayout.hpp"
#include "codegen/common/objfile/ObjectFileWriter.hpp"
#include "codegen/x86_64/Backend.hpp"
#include "il/core/Global.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"

#include <string>
#include <utility>

namespace viper::codegen::x64::passes {

namespace {

/// @brief Map a backend target-platform enum to its native object-file format.
/// @details Mirrors Backend.cpp's (anonymous-namespace) targetObjectFormat so the
///          assembly-text `.data` directives match the object the linker expects.
objfile::ObjFormat emitTargetObjectFormat(CodegenOptions::TargetPlatform platform) {
    switch (platform) {
        case CodegenOptions::TargetPlatform::Darwin:
            return objfile::ObjFormat::MachO;
        case CodegenOptions::TargetPlatform::Linux:
            return objfile::ObjFormat::ELF;
        case CodegenOptions::TargetPlatform::Windows:
            return objfile::ObjFormat::COFF;
        case CodegenOptions::TargetPlatform::Host:
            break;
    }
    return objfile::detectHostFormat();
}

/// @brief Emit a `.data` section (assembly text) for a module's writable scalar globals.
/// @details Mirrors the binary path (BinaryEmitPass::buildScalarDataSection) and the
///          AArch64 RodataPool::emitData asm path so `--system-asm` output is parity
///          with `-run-native`. Symbol mangling matches AsmEmitter::formatSymbolReference
///          (leading `_` on Mach-O); section directives follow the object format.
std::string emitScalarDataSectionAsm(const il::core::Module &mod, objfile::ObjFormat format) {
    std::string out;
    bool wroteHeader = false;
    for (const auto &g : mod.globals) {
        const auto layout = common::scalarGlobalLayout(g.type.kind);
        const int sizeBytes = layout.sizeBytes;
        const bool isFloat = layout.isFloat;
        if (sizeBytes == 0)
            continue;
        if (!wroteHeader) {
            if (format == objfile::ObjFormat::ELF)
                out += ".data\n";
            else if (format == objfile::ObjFormat::COFF)
                out += ".section .data,\"dw\"\n";
            else
                out += ".section __DATA,__data\n";
            wroteHeader = true;
        }
        const std::string sym =
            (format == objfile::ObjFormat::MachO) ? "_" + g.name : g.name;
        const int p2align = sizeBytes >= 8 ? 3 : sizeBytes >= 4 ? 2 : sizeBytes >= 2 ? 1 : 0;
        const char *dir = isFloat            ? ".double"
                          : sizeBytes == 8   ? ".quad"
                          : sizeBytes == 4   ? ".long"
                          : sizeBytes == 2   ? ".short"
                                             : ".byte";
        std::string value = g.init;
        const auto b = value.find_first_not_of(" \t\r\n");
        const auto e = value.find_last_not_of(" \t\r\n");
        value = (b == std::string::npos) ? std::string() : value.substr(b, e - b + 1);
        if (value.empty())
            value = isFloat ? "0.0" : "0";
        out += "  .p2align " + std::to_string(p2align) + "\n";
        out += "  .globl " + sym + "\n";
        out += sym + ":\n";
        out += "  " + std::string(dir) + " " + value + "\n";
    }
    if (wroteHeader)
        out += "\n";
    return out;
}

} // namespace

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

    // Append a .data section for writable scalar globals so the assembly-text path
    // defines the symbols that `gaddr`/`leaq` reference (binary path parity).
    result.asmText +=
        emitScalarDataSectionAsm(module.il, emitTargetObjectFormat(options_.targetPlatform));

    module.codegenResult = std::move(result);
    return true;
}

} // namespace viper::codegen::x64::passes
