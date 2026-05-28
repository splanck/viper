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

#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace viper::codegen::aarch64::passes {

namespace {

/// @brief Register the source file for every file_id referenced across all functions.
/// @param debugSourcePath Path to the original source; "<source>" if empty.
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
        table.addFileSlot(filePath);
}

/// @brief Register the source file for every file_id referenced in a single function.
/// @param debugSourcePath Path to the original source; "<source>" if empty.
void seedDebugFiles(DebugLineTable &table, const MFunction &fn, std::string_view debugSourcePath) {
    uint32_t maxFileId = 1;
    for (const auto &bb : fn.blocks) {
        for (const auto &mi : bb.instrs) {
            if (mi.loc.file_id > maxFileId)
                maxFileId = mi.loc.file_id;
        }
    }

    std::string filePath = std::string(debugSourcePath);
    if (filePath.empty())
        filePath = "<source>";
    else
        filePath = std::filesystem::path(filePath).lexically_normal().string();

    for (uint32_t fileId = 1; fileId <= maxFileId; ++fileId)
        table.addFileSlot(filePath);
}

} // namespace

/// @brief Encode AArch64 MIR functions to raw machine code.
/// @details Validates @p module.ti, allocates a per-function CodeSection,
///          drives @ref A64BinaryEncoder for each function, and stores the
///          resulting bytes plus rodata pool into @p module.binaryText* /
///          binaryRodata for the object-file writer to consume.
/// @return true on success; on failure records diagnostics via @p diags.
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

    objfile::CodeSection rodata;

    // Seed rodata before encoding so cross-section fixups can identify
    // same-object rodata targets without relying on writer-side heuristics.
    for (const auto &[label, content] : module.rodataPool.entries()) {
        rodata.defineSymbol(label, objfile::SymbolBinding::Local, objfile::SymbolSection::Rodata);
        rodata.emitBytes(content.data(), content.size());
        rodata.emit8(0); // NUL terminator
    }

    // Set up debug line table for address→line mapping when requested.
    viper::codegen::DebugLineTable debugLines;
    if (module.emitDebugLines)
        seedDebugFiles(debugLines, module.mir, module.debugSourcePath);
    uint64_t debugBias = 0;

    auto encodeSequentially = [&]() {
        for (const auto &fn : module.mir) {
            // Emit each function into its own CodeSection for per-function dead stripping.
            module.binaryTextSections.emplace_back();
            viper::codegen::DebugLineTable funcDebugLines;
            if (module.emitDebugLines)
                seedDebugFiles(funcDebugLines, fn, module.debugSourcePath);
            binenc::A64BinaryEncoder funcEncoder;
            if (module.emitDebugLines)
                funcEncoder.setDebugLineTable(&funcDebugLines);
            try {
                funcEncoder.encodeFunction(fn, module.binaryTextSections.back(), rodata, abi);
            } catch (const std::exception &ex) {
                module.binaryTextSections.pop_back();
                diags.error("BinaryEmitPass: failed to encode AArch64 function '" + fn.name +
                            "': " + ex.what());
                return false;
            }
            if (module.emitDebugLines)
                debugLines.append(funcDebugLines, debugBias);
            debugBias += static_cast<uint64_t>(module.binaryTextSections.back().currentOffset());
        }
        return true;
    };

    const unsigned workerLimit = std::thread::hardware_concurrency();
    if (module.emitDebugLines || module.mir.size() < 2 || workerLimit < 2) {
        if (!encodeSequentially())
            return false;
    } else {
        struct EncodedFunction {
            objfile::CodeSection text;
            std::string error;
        };

        std::vector<EncodedFunction> encoded(module.mir.size());
        std::atomic_size_t next{0};
        std::atomic_bool failed{false};
        const size_t workerCount =
            std::min<size_t>(module.mir.size(), static_cast<size_t>(workerLimit));
        std::vector<std::thread> workers;
        workers.reserve(workerCount);

        for (size_t worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&]() {
                while (!failed.load(std::memory_order_relaxed)) {
                    const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= module.mir.size())
                        return;

                    binenc::A64BinaryEncoder encoder;
                    try {
                        encoder.encodeFunction(module.mir[index], encoded[index].text, rodata, abi);
                    } catch (const std::exception &ex) {
                        encoded[index].error = ex.what();
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
            });
        }

        for (auto &worker : workers)
            worker.join();

        for (size_t i = 0; i < encoded.size(); ++i) {
            if (!encoded[i].error.empty()) {
                diags.error("BinaryEmitPass: failed to encode AArch64 function '" +
                            module.mir[i].name + "': " + encoded[i].error);
                return false;
            }
        }

        module.binaryTextSections.reserve(encoded.size());
        for (auto &fn : encoded)
            module.binaryTextSections.push_back(std::move(fn.text));
    }

    if (module.coalesceTextSections && module.binaryTextSections.size() > 1) {
        objfile::CodeSection merged;
        for (const auto &section : module.binaryTextSections)
            merged.appendSection(section);
        module.binaryTextSections.clear();
        module.binaryTextSections.push_back(std::move(merged));
    }

    // Encode DWARF .debug_line if any entries were recorded.
    if (module.emitDebugLines && !debugLines.empty())
        module.debugLineData = debugLines.encodeDwarf5(8);

    objfile::CodeSection mergedText;
    for (const auto &section : module.binaryTextSections)
        mergedText.appendSection(section);
    module.binaryText = std::move(mergedText);
    module.binaryRodata = std::move(rodata);
    return true;
}

} // namespace viper::codegen::aarch64::passes
