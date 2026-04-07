//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/BinaryEmitPass.cpp
// Purpose: Encode AArch64 MIR into machine code bytes via A64BinaryEncoder,
//          producing CodeSection output for direct .o emission.
// Key invariants:
//   - Requires register allocation to have completed (operates on physical regs)
//   - Populates AArch64Module::binaryText and AArch64Module::binaryRodata
//   - RodataPool entries emitted as raw bytes with NUL terminators (.asciz semantics)
// Ownership/Lifetime:
//   - Stateless pass; mutates AArch64Module binary fields
// Links: codegen/aarch64/binenc/A64BinaryEncoder.hpp
//        codegen/aarch64/RodataPool.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/BinaryEmitPass.hpp"

#include "codegen/aarch64/binenc/A64BinaryEncoder.hpp"
#include "codegen/common/objfile/DebugLineTable.hpp"

#include <exception>
#include <filesystem>
#include <string_view>
#include <utility>

namespace viper::codegen::aarch64::passes {

namespace {

void seedDebugFiles(DebugLineTable &table,
                    const std::vector<MFunction> &mir,
                    std::string_view debugSourcePath) {
    uint32_t maxFileId = 1;
    for (const auto &fn : mir) {
        for (const auto &bb : fn.blocks) {
            for (const auto &mi : bb.instrs) {
                if (mi.loc.file_id > maxFileId)
                    maxFileId = mi.loc.file_id;
            }
        }
    }

    std::string filePath = std::string(debugSourcePath);
    if (filePath.empty())
        filePath = "<source>";
    else
        filePath = std::filesystem::path(filePath).lexically_normal().string();

    for (uint32_t fileId = 1; fileId <= maxFileId; ++fileId)
        table.addFile(filePath);
}

} // namespace

bool BinaryEmitPass::run(AArch64Module &module, Diagnostics &diags) {
    if (!module.ti) {
        diags.error("BinaryEmitPass: ti must be non-null");
        return false;
    }

    module.binaryTextSections.clear();
    module.debugLineData.clear();

    if (module.mir.empty()) {
        // Not an error — modules with no functions produce empty output.
        module.binaryText.emplace();
        module.binaryRodata.emplace();
        return true;
    }

    const auto abi = module.ti->abiFormat;

    objfile::CodeSection text;
    objfile::CodeSection rodata;

    // Seed rodata before encoding so cross-section fixups can identify
    // same-object rodata targets without relying on writer-side heuristics.
    for (const auto &[label, content] : module.rodataPool.entries()) {
        rodata.defineSymbol(label, objfile::SymbolBinding::Local, objfile::SymbolSection::Rodata);
        rodata.emitBytes(content.data(), content.size());
        rodata.emit8(0); // NUL terminator
    }

    // Set up debug line table for address→line mapping.
    viper::codegen::DebugLineTable debugLines;
    seedDebugFiles(debugLines, module.mir, module.debugSourcePath);

    for (const auto &fn : module.mir) {
        // Emit each function into its own CodeSection for per-function dead stripping.
        module.binaryTextSections.emplace_back();
        viper::codegen::DebugLineTable funcDebugLines;
        seedDebugFiles(funcDebugLines, std::vector<MFunction>{fn}, module.debugSourcePath);
        binenc::A64BinaryEncoder funcEncoder;
        funcEncoder.setDebugLineTable(&funcDebugLines);
        MFunction emitFn = fn;
        if (emitFn.name == "main" && !emitFn.blocks.empty()) {
            emitFn.isLeaf = false;
            auto &entryInstrs = emitFn.blocks.front().instrs;
            entryInstrs.insert(entryInstrs.begin(),
                               {MInstr{MOpcode::Bl, {MOperand::labelOp("rt_legacy_context")}},
                                MInstr{MOpcode::Bl, {MOperand::labelOp("rt_set_current_context")}}});
        }
        try {
            funcEncoder.encodeFunction(emitFn, module.binaryTextSections.back(), rodata, abi);
        } catch (const std::exception &ex) {
            module.binaryTextSections.pop_back();
            diags.error("BinaryEmitPass: failed to encode AArch64 function '" + fn.name +
                        "': " + ex.what());
            return false;
        }
        const uint64_t debugBias = static_cast<uint64_t>(text.currentOffset());
        debugLines.append(funcDebugLines, debugBias);
        text.appendSection(module.binaryTextSections.back());
    }

    // Encode DWARF .debug_line if any entries were recorded.
    if (!debugLines.empty())
        module.debugLineData = debugLines.encodeDwarf5(8);

    module.binaryText = std::move(text);
    module.binaryRodata = std::move(rodata);
    return true;
}

} // namespace viper::codegen::aarch64::passes
