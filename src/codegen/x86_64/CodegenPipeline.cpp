//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/CodegenPipeline.cpp
// Purpose: Implement the reusable IL-to-x86-64 compilation pipeline used by
//          CLI front ends. Coordinates module loading, verification, backend
//          pass execution, and optional linking/execution.
// Key invariants:
//   - Passes execute sequentially with early exits on failure, ensuring
//     diagnostics are recorded deterministically.
//   - No partial artefacts leak on error.
//   - Host ABI selection, native-link fallback, and system tool invocation
//     are resolved at runtime based on TargetPlatform.
// Cross-platform touchpoints:
//   - Native-link archive discovery and platform-specific linker options are
//     routed through codegen/common/LinkerSupport and NativeLinker.
// Ownership/Lifetime:
//   - The pipeline borrows IL modules and writes assembly/binaries to
//     caller-specified locations without assuming ownership of resources.
// Links: codegen/x86_64/CodegenPipeline.hpp,
//        codegen/x86_64/Backend.hpp,
//        codegen/x86_64/passes/PassManager.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/CodegenPipeline.hpp"

#include "codegen/common/LinkerSupport.hpp"
#include "codegen/common/NativeEHLowering.hpp"
#include "codegen/common/linker/NativeLinker.hpp"
#include "codegen/common/objfile/ObjectFileWriter.hpp"
#include "codegen/x86_64/passes/BinaryEmitPass.hpp"
#include "codegen/x86_64/passes/EmitPass.hpp"
#include "codegen/x86_64/passes/LegalizePass.hpp"
#include "codegen/x86_64/passes/LoweringPass.hpp"
#include "codegen/x86_64/passes/PassManager.hpp"
#include "codegen/x86_64/passes/PeepholePass.hpp"
#include "codegen/x86_64/passes/PreRegAllocOptPass.hpp"
#include "codegen/x86_64/passes/RegAllocPass.hpp"
#include "codegen/x86_64/passes/SchedulerPass.hpp"
#include "il/transform/PassManager.hpp"
#include "tools/common/module_loader.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace viper::codegen::x64 {
namespace {

using TargetPlatform = CodegenOptions::TargetPlatform;

/// @brief Resolve the concrete target platform from pipeline options.
/// @details Honors an explicit non-Host @c target_platform; otherwise infers
///          it from the ABI (Win64 → Windows) or the detected host linker
///          platform. Falls back to Linux if detection is inconclusive.
[[nodiscard]] TargetPlatform effectiveTargetPlatform(const CodegenPipeline::Options &opts) {
    if (opts.target_platform != TargetPlatform::Host)
        return opts.target_platform;
    if (opts.target_abi == CodegenOptions::TargetABI::Win64)
        return TargetPlatform::Windows;
    switch (linker::detectLinkPlatform()) {
        case linker::LinkPlatform::macOS:
            return TargetPlatform::Darwin;
        case linker::LinkPlatform::Windows:
            return TargetPlatform::Windows;
        case linker::LinkPlatform::Linux:
            return TargetPlatform::Linux;
    }
    return TargetPlatform::Linux;
}

/// @brief Map a target platform to its native object-file format
///        (Darwin→Mach-O, Linux→ELF, Windows→COFF; Host→detected).
[[nodiscard]] objfile::ObjFormat targetObjectFormat(TargetPlatform platform) {
    switch (platform) {
        case TargetPlatform::Darwin:
            return objfile::ObjFormat::MachO;
        case TargetPlatform::Linux:
            return objfile::ObjFormat::ELF;
        case TargetPlatform::Windows:
            return objfile::ObjFormat::COFF;
        case TargetPlatform::Host:
            return objfile::detectHostFormat();
    }
    return objfile::detectHostFormat();
}

/// @brief Map a target platform to the linker's LinkPlatform enum
///        (Host resolves via the detected host linker platform).
[[nodiscard]] linker::LinkPlatform targetLinkPlatform(TargetPlatform platform) {
    switch (platform) {
        case TargetPlatform::Darwin:
            return linker::LinkPlatform::macOS;
        case TargetPlatform::Linux:
            return linker::LinkPlatform::Linux;
        case TargetPlatform::Windows:
            return linker::LinkPlatform::Windows;
        case TargetPlatform::Host:
            return linker::detectLinkPlatform();
    }
    return linker::detectLinkPlatform();
}

/// @brief Build the system assembler command prefix for @p platform.
/// @details On a native Darwin host uses `cc`; cross-targets use `clang`
///          with the matching `--target=` x86-64 triple for the target OS.
[[nodiscard]] std::vector<std::string> systemAssemblerArgs(TargetPlatform platform) {
    switch (platform) {
        case TargetPlatform::Darwin:
#if defined(__APPLE__)
            return {"cc", "-arch", "x86_64"};
#else
            return {"clang", "--target=x86_64-apple-macos11"};
#endif
        case TargetPlatform::Linux:
            return {"clang", "--target=x86_64-unknown-linux-gnu"};
        case TargetPlatform::Windows:
            return {"clang", "--target=x86_64-pc-windows-msvc"};
        case TargetPlatform::Host:
            switch (targetLinkPlatform(TargetPlatform::Host)) {
                case linker::LinkPlatform::macOS:
                    return systemAssemblerArgs(TargetPlatform::Darwin);
                case linker::LinkPlatform::Windows:
                    return systemAssemblerArgs(TargetPlatform::Windows);
                case linker::LinkPlatform::Linux:
                    return systemAssemblerArgs(TargetPlatform::Linux);
            }
    }
    return {"clang", "--target=x86_64-unknown-linux-gnu"};
}

/// @brief Return the first path in @p candidates that exists on disk, else empty.
/// @details Used to probe Release / Debug / no-config sub-paths under CMake
///          build directories so the same code works against single- and
///          multi-config generators.
std::filesystem::path pickFirstExisting(std::initializer_list<std::filesystem::path> candidates) {
    for (const auto &candidate : candidates) {
        if (common::fileExists(candidate))
            return candidate;
    }
    return std::filesystem::path{};
}

/// @brief Compute the output assembly path from pipeline options.
/// @details Falls back to sensible defaults when the input path is empty
///          or refers to a directory, mirroring traditional compiler
///          behaviour.
/// @param opts Pipeline configuration specifying the IL input path.
/// @return Filesystem location for the generated assembly file.
std::filesystem::path deriveAssemblyPath(const CodegenPipeline::Options &opts) {
    std::filesystem::path assembly = std::filesystem::path(opts.input_il_path);
    if (assembly.empty()) {
        return std::filesystem::path("out.s");
    }
    assembly.replace_extension(".s");
    if (assembly.filename().empty()) {
        assembly = assembly.parent_path() / "out.s";
    }
    return assembly;
}

/// @brief Determine the executable output path based on user input.
/// @details Strips the IL extension when present and ensures the result has
///          a filename component so the linker output is predictable.
///          On Windows, adds the .exe extension.
/// @param opts Pipeline configuration describing the IL input.
/// @return Filesystem path for the linked executable.
std::filesystem::path deriveExecutablePath(const CodegenPipeline::Options &opts) {
    const bool isWindowsTarget = effectiveTargetPlatform(opts) == TargetPlatform::Windows;
    std::filesystem::path exe = std::filesystem::path(opts.input_il_path);
    if (exe.empty()) {
        return isWindowsTarget ? std::filesystem::path("a.exe") : std::filesystem::path("a.out");
    }
    exe.replace_extension("");
    if (exe.filename().empty() || exe.filename() == ".") {
        return isWindowsTarget ? (exe.parent_path() / "a.exe") : (exe.parent_path() / "a.out");
    }
    if (isWindowsTarget)
        exe.replace_extension(".exe");
    return exe;
}

/// @brief Persist generated assembly to disk.
/// @details Writes @p text to @p path, reporting any failures to the
///          provided error stream. The helper returns @c false when I/O
///          errors occur so the pipeline can stop before invoking the linker.
/// @param path Destination path for the assembly file.
/// @param text Assembly text to write.
/// @param err  Stream receiving human-readable error messages.
/// @return @c true when the file was written successfully.
bool writeAssemblyFile(const std::filesystem::path &path,
                       const std::string &text,
                       std::ostream &err) {
    return common::writeTextFile(path, text, err);
}

/// @brief Convert a path to use native separators on the current platform.
/// @details On Windows, forward slashes in paths can confuse cmd.exe when
///          passed through run_process. This helper ensures backslashes are used.
/// @param path Original path to normalize.
/// @return String with platform-native path separators.
std::string toNativePath(const std::filesystem::path &path) {
    std::filesystem::path native = path;
    native.make_preferred();
    return native.string();
}

/// @brief Assemble emitted assembly into an object file.
/// @details Invokes the system C compiler with the `-c` flag so the pipeline
///          can stop after producing a relocatable object when no executable is
///          required.
/// @param asmPath Path to the assembly file to assemble.
/// @param objPath Destination path for the object file.
/// @param out     Stream receiving the assembler's standard output.
/// @param err     Stream receiving the assembler's standard error.
/// @return Normalised assembler exit code (-1 when the command could not start).
int invokeAssembler(const std::filesystem::path &asmPath,
                    const std::filesystem::path &objPath,
                    TargetPlatform targetPlatform,
                    std::ostream &out,
                    std::ostream &err) {
    const std::vector<std::string> ccArgs = systemAssemblerArgs(targetPlatform);
    const int exitCode =
        common::invokeAssembler(ccArgs, toNativePath(asmPath), toNativePath(objPath), out, err);
    if (exitCode != 0) {
        err << "error: assembler driver exited with status " << exitCode << "\n";
    }
    return exitCode;
}

/// @brief Execute a freshly linked binary and capture its output.
/// @details Delegates to the shared executable runner after normalising the
///          path representation for the host platform.
/// @param exePath Path to the executable to run.
/// @param out     Stream receiving program stdout.
/// @param err     Stream receiving program stderr.
/// @return Process exit code (-1 when the process could not be started).
int runExecutable(const std::filesystem::path &exePath, std::ostream &out, std::ostream &err) {
    return common::runExecutable(toNativePath(exePath), out, err);
}

/// @brief Append runtime archive paths required by @p ctx in dependency order.
/// @details Deduplicates by absolute path. On Windows, when the Base component
///          is required, also pulls in Oop/Arrays/Collections/Threads/Text/IoFs
///          because Windows CRT startup expects them. Graphics and Audio
///          support libraries are appended when the corresponding runtime
///          components are present in the link context.
void collectNativeLinkArchives(const common::LinkContext &ctx, std::vector<std::string> &archives) {
    std::unordered_set<std::string> seenArchives;

    auto appendIfExists = [&](const std::filesystem::path &path) {
        if (!common::fileExists(path))
            return;
        const std::string archivePath = path.lexically_normal().string();
        if (seenArchives.insert(archivePath).second)
            archives.push_back(archivePath);
    };

    auto appendComponent = [&](RtComponent comp) {
        appendIfExists(common::runtimeArchivePath(ctx.buildDir, archiveNameForComponent(comp)));
    };

    for (const auto &entry : ctx.requiredArchives)
        appendIfExists(entry.second);

    // The Windows runtime build does not preserve the Unix weak-link defaults
    // used by viper_rt_base. Pull in the concrete component archives that
    // satisfy those cross-component references without regressing to
    // "link every runtime archive".
#if defined(_WIN32)
    if (common::hasComponent(ctx, RtComponent::Base)) {
        appendComponent(RtComponent::Oop);
        appendComponent(RtComponent::Arrays);
        appendComponent(RtComponent::Collections);
        appendComponent(RtComponent::Threads);
        appendComponent(RtComponent::Text);
        appendComponent(RtComponent::IoFs);
    }
#endif

    if (common::hasComponent(ctx, RtComponent::Graphics)) {
        const auto guiLib = common::supportLibraryPath(ctx.buildDir, "vipergui");
        const auto gfxLib = common::supportLibraryPath(ctx.buildDir, "vipergfx");
        appendIfExists(guiLib);
        appendIfExists(gfxLib);
    }

    if (common::hasComponent(ctx, RtComponent::Audio)) {
        const auto audLib = common::supportLibraryPath(ctx.buildDir, "viperaud");
        appendIfExists(audLib);
    }

    // Embedding the Zia frontend pulls in il_runtime's RuntimeRegistry, which
    // references the entire rt_* catalog regardless of what the program itself
    // uses. Link every runtime component so those references resolve.
    if (ctx.needsZiaFrontend) {
        for (int i = 0; i < static_cast<int>(RtComponent::Count); ++i)
            appendComponent(static_cast<RtComponent>(i));
    }
}

/// @brief Link @p objPath into @p exePath using Viper's in-process linker.
/// @details Fills NativeLinkerOptions from @p ctx and the caller's flags,
///          collects archive paths via collectNativeLinkArchives, and invokes
///          linker::nativeLink. Errors are written to @p err and surfaced via
///          a non-zero return value.
int linkObjectWithNativeLinker(const std::filesystem::path &objPath,
                               const std::filesystem::path &exePath,
                               const common::LinkContext &ctx,
                               TargetPlatform targetPlatform,
                               const std::vector<std::string> &extraObjects,
                               std::size_t stackSize,
                               bool fastLink,
                               bool preserveDebugSections,
                               std::optional<bool> windowsDebugRuntime,
                               std::ostream &out,
                               std::ostream &err) {
    linker::NativeLinkerOptions linkOpts;
    linkOpts.objPath = objPath.string();
    linkOpts.exePath = exePath.string();
    linkOpts.entrySymbol = "main";
    linkOpts.platform = targetLinkPlatform(targetPlatform);
    linkOpts.arch = linker::LinkArch::X86_64;
    linkOpts.stackSize = stackSize;
    linkOpts.fastLink = fastLink;
    linkOpts.preserveDebugSections = preserveDebugSections;
    linkOpts.windowsDebugRuntime = windowsDebugRuntime;
    collectNativeLinkArchives(ctx, linkOpts.archivePaths);
    if (ctx.needsZiaFrontend) {
        const auto ziaEditorLib = common::supportLibraryPath(ctx.buildDir, "zia_editor_services");
        if (common::fileExists(ziaEditorLib))
            linkOpts.forceLoadArchivePaths.push_back(ziaEditorLib.lexically_normal().string());
        // zia_editor_services' static-link closure (fe_zia plus IL
        // build/verify/transform/runtime/core/support). Demand-driven: only
        // members the force-loaded editor-service objects reference are
        // extracted.
        for (const auto &lib : common::ziaFrontendClosureLibs()) {
            const auto p = common::supportLibraryPath(ctx.buildDir, lib);
            if (common::fileExists(p))
                linkOpts.archivePaths.push_back(p.lexically_normal().string());
        }
    }
    linkOpts.extraObjPaths = extraObjects;
    return linker::nativeLink(linkOpts, out, err);
}

} // namespace

/// @brief Construct a pipeline with the given configuration options.
/// @details Copies the option struct so the pipeline retains a stable
///          configuration even if the caller mutates their original instance.
/// @param opts Pipeline configuration (input path, action flags, etc.).
CodegenPipeline::CodegenPipeline(Options opts) : opts_(std::move(opts)) {}

/// @brief Run the configured pipeline from IL loading to optional execution.
/// @details Loads and verifies the IL module, executes the backend pass
///          manager, writes assembly files, optionally links, and optionally
///          runs the resulting executable. All diagnostics are aggregated into
///          the returned @ref PipelineResult.
/// @return Struct summarising exit code, stdout, and stderr output.
PipelineResult CodegenPipeline::run() {
    PipelineResult result{0, "", ""};
    std::ostringstream out;
    std::ostringstream err;
    auto finish = [&]() -> PipelineResult {
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    };

    il::core::Module module;
    const auto loadResult = il::tools::common::loadModuleFromFile(opts_.input_il_path, module, err);
    if (!loadResult.succeeded()) {
        result.exit_code = 1;
        return finish();
    }
    if (!il::tools::common::verifyModule(module, err)) {
        result.exit_code = 1;
        return finish();
    }

    return runWithModule(std::move(module), opts_.input_il_path, true);
}

PipelineResult CodegenPipeline::runWithModule(il::core::Module module,
                                              std::string debugSourcePath,
                                              bool moduleAlreadyVerified) {
    PipelineResult result{0, "", ""};
    std::ostringstream out;
    std::ostringstream err;

    // Flush the accumulated stdout/stderr buffers into `result` and return it.
    // Used at every pipeline exit (success and failure) to avoid repeating the
    // same three-line epilogue ~26 times.
    auto finish = [&]() -> PipelineResult {
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    };

    if (debugSourcePath.empty())
        debugSourcePath = opts_.input_il_path;

    if (!moduleAlreadyVerified && !il::tools::common::verifyModule(module, err)) {
        result.exit_code = 1;
        return finish();
    }

    viper::codegen::common::lowerNativeEh(module);
    if (const auto residualEh = viper::codegen::common::findResidualStructuredEh(module)) {
        err << "error: " << *residualEh << "\n";
        result.exit_code = 1;
        return finish();
    }

    // Run the canonical IL optimization pipeline before lowering to MIR so native
    // backends stay aligned with frontend/VM optimization behavior.
    if (!opts_.skip_il_optimization && opts_.optimize >= 1) {
        il::transform::PassManager ilpm;
        const bool ok = ilpm.runPipeline(module, opts_.optimize >= 2 ? "O2" : "O1");

        if (!ok) {
            err << "error: failed to run x86-64 IL optimization pipeline\n";
            result.exit_code = 1;
            return finish();
        }
        // Re-verify IL after optimization to catch optimizer bugs early.
        if (!il::tools::common::verifyModule(module, err)) {
            err << "error: IL verification failed after optimization\n";
            result.exit_code = 1;
            return finish();
        }
    }

    passes::Module pipelineModule{};
    pipelineModule.il = std::move(module);

    const bool useNativeAsm = (opts_.assembler_mode == AssemblerMode::Native);
    if (!useNativeAsm && !opts_.asset_blob_path.empty() && opts_.extra_objects.empty()) {
        err << "error: x64 --asset-blob requires --native-asm or a companion --extra-obj\n";
        result.exit_code = 1;
        return finish();
    }

    CodegenOptions codegenOpts{};
    const TargetPlatform targetPlatform = effectiveTargetPlatform(opts_);
    codegenOpts.optimizeLevel = opts_.optimize;
    codegenOpts.targetABI = opts_.target_abi;
    codegenOpts.targetPlatform = targetPlatform;
    codegenOpts.debugSourcePath = debugSourcePath;
    codegenOpts.emitDebugLines = opts_.emit_debug_lines;
    pipelineModule.options = codegenOpts;
    pipelineModule.target = &selectTarget(pipelineModule.options.targetABI);

    passes::Diagnostics diagnostics{};
    passes::PassManager manager{};
    if (opts_.time_passes)
        manager.setTimingStream(&err, "x86_64");
    manager.addPass(std::make_unique<passes::LoweringPass>());
    manager.addPass(std::make_unique<passes::LegalizePass>());
    if (codegenOpts.optimizeLevel >= 1)
        manager.addPass(std::make_unique<passes::PreRegAllocOptPass>());
    manager.addPass(std::make_unique<passes::RegAllocPass>());
    manager.addPass(std::make_unique<passes::SchedulerPass>());
    manager.addPass(std::make_unique<passes::PeepholePass>());

    if (useNativeAsm) {
        manager.addPass(std::make_unique<passes::BinaryEmitPass>(codegenOpts));
    } else {
        manager.addPass(std::make_unique<passes::EmitPass>(codegenOpts));
    }

    if (!manager.run(pipelineModule, diagnostics)) {
        diagnostics.flush(err);
        result.exit_code = 1;
        return finish();
    }

    diagnostics.flush(err);

    // --- Inject asset blob into .rodata (if present) ---
    if (useNativeAsm && pipelineModule.binaryRodata && !opts_.asset_blob_path.empty()) {
        std::ifstream af(opts_.asset_blob_path, std::ios::binary | std::ios::ate);
        if (af.is_open()) {
            auto blobSize = af.tellg();
            if (blobSize > 0) {
                af.seekg(0);
                std::vector<uint8_t> assetBlob(static_cast<size_t>(blobSize));
                af.read(reinterpret_cast<char *>(assetBlob.data()), blobSize);

                const char *blobLabel = "viper_asset_blob";
                const char *sizeLabel = "viper_asset_blob_size";
                auto &rodata = *pipelineModule.binaryRodata;
                rodata.alignTo(16);
                rodata.defineSymbol(
                    blobLabel, objfile::SymbolBinding::Global, objfile::SymbolSection::Rodata);
                rodata.emitBytes(assetBlob.data(), assetBlob.size());
                rodata.alignTo(8);
                rodata.defineSymbol(
                    sizeLabel, objfile::SymbolBinding::Global, objfile::SymbolSection::Rodata);
                rodata.emit64LE(static_cast<uint64_t>(assetBlob.size()));
            }
        }
    }

    // --- Native assembler path: write .o directly via ObjectFileWriter ---
    if (useNativeAsm) {
        if (!pipelineModule.binaryText && pipelineModule.binaryTextSections.empty()) {
            err << "error: binary emit pass did not produce machine code\n";
            result.exit_code = 1;
            return finish();
        }

        // If user requested assembly text output via -S, we still need text emit.
        // Fall through to the text path by also running text emit.
        if (opts_.emit_asm && !opts_.output_asm_path.empty()) {
            // For -S with --native-asm, we don't produce .s — just stop after .o.
            // Users should use --system-asm if they want assembly text.
            err << "warning: -S is not supported with --native-asm; ignoring -S\n";
        }

        // Determine object file path.
        auto looksLikeObjectFile = [](const std::string &path) -> bool {
            const std::size_t dotPos = path.rfind('.');
            if (dotPos == std::string::npos)
                return false;
            const std::string ext = path.substr(dotPos);
            return ext == ".o" || ext == ".obj";
        };

        const bool wantsObjectOnly = !opts_.output_obj_path.empty() && !opts_.run_native &&
                                     looksLikeObjectFile(opts_.output_obj_path);

        // Derive .o path from IL path.
        std::filesystem::path objPath;
        if (wantsObjectOnly) {
            objPath = opts_.output_obj_path;
        } else {
            objPath = std::filesystem::path(opts_.input_il_path);
            objPath.replace_extension(".o");
        }

        // Write the object file using the native writer for x86_64.
        // Must specify ObjArch::X86_64 explicitly — createHostObjectFileWriter()
        // would pick the host arch (e.g. arm64 on Apple Silicon).
        auto writer = objfile::createObjectFileWriter(targetObjectFormat(targetPlatform),
                                                      objfile::ObjArch::X86_64);
        if (!writer) {
            err << "error: no native object file writer for this platform\n";
            result.exit_code = 1;
            return finish();
        }

        // Pass pre-encoded DWARF .debug_line data to the writer.
        const bool hasDebugLine = !pipelineModule.debugLineData.empty();
        if (hasDebugLine && !pipelineModule.binaryText) {
            err << "error: binary emit pass did not produce merged debug machine code\n";
            result.exit_code = 1;
            return finish();
        }
        if (hasDebugLine)
            writer->setDebugLineData(std::move(pipelineModule.debugLineData));

        const bool wroteObject = hasDebugLine ? writer->write(objPath.string(),
                                                              *pipelineModule.binaryText,
                                                              *pipelineModule.binaryRodata,
                                                              err)
                                              : writer->write(objPath.string(),
                                                              pipelineModule.binaryTextSections,
                                                              *pipelineModule.binaryRodata,
                                                              err);
        if (!wroteObject) {
            result.exit_code = 1;
            return finish();
        }

        if (wantsObjectOnly) {
            result.exit_code = 0;
            return finish();
        }

        // Link the emitted object with the native linker, deriving the runtime
        // archive set from the binary symbol table because there is no assembly
        // text on the native-assembler path.
        const std::filesystem::path exePath = opts_.output_obj_path.empty()
                                                  ? deriveExecutablePath(opts_)
                                                  : std::filesystem::path(opts_.output_obj_path);

        std::unordered_set<std::string> extSymbols;
        if (!pipelineModule.binaryTextSections.empty()) {
            for (const auto &section : pipelineModule.binaryTextSections) {
                for (const auto &sym : section.symbols()) {
                    if (sym.binding == objfile::SymbolBinding::External)
                        extSymbols.insert(sym.name);
                }
            }
        } else if (pipelineModule.binaryText) {
            for (const auto &sym : pipelineModule.binaryText->symbols()) {
                if (sym.binding == objfile::SymbolBinding::External)
                    extSymbols.insert(sym.name);
            }
        }
        if (pipelineModule.binaryRodata) {
            for (const auto &sym : pipelineModule.binaryRodata->symbols()) {
                if (sym.binding == objfile::SymbolBinding::External)
                    extSymbols.insert(sym.name);
            }
        }

        common::LinkContext ctx;
        if (const int rc = common::prepareLinkContextFromSymbols(extSymbols, ctx, out, err);
            rc != 0) {
            result.exit_code = 1;
            return finish();
        }

        if (opts_.link_mode == LinkMode::System)
            err << "warning: --system-link is deprecated; using the native linker\n";

        const int linkExit = linkObjectWithNativeLinker(objPath,
                                                        exePath,
                                                        ctx,
                                                        targetPlatform,
                                                        opts_.extra_objects,
                                                        opts_.stack_size,
                                                        opts_.fast_link,
                                                        opts_.emit_debug_lines,
                                                        opts_.windows_debug_runtime,
                                                        out,
                                                        err);
        if (linkExit != 0) {
            result.exit_code = linkExit == -1 ? 1 : linkExit;
            return finish();
        }

        // Clean up intermediate .o file.
        {
            std::error_code ec;
            std::filesystem::remove(objPath, ec);
        }

        if (!opts_.run_native) {
            result.exit_code = 0;
            return finish();
        }

        const int runExit = runExecutable(exePath, out, err);
        result.exit_code = (runExit == -1) ? 1 : runExit;
        return finish();
    }

    // --- System assembler path (existing): text assembly → cc -c → .o ---
    if (!pipelineModule.codegenResult) {
        err << "error: emit pass did not produce assembly output\n";
        result.exit_code = 1;
        return finish();
    }

    const std::string &asmText = pipelineModule.codegenResult->asmText;

    const std::filesystem::path asmPath = opts_.output_asm_path.empty()
                                              ? deriveAssemblyPath(opts_)
                                              : std::filesystem::path(opts_.output_asm_path);
    if (!writeAssemblyFile(asmPath, asmText, err)) {
        result.exit_code = 1;
        return finish();
    }

    // If user requested assembly output via -S with a specific path, stop here.
    // Don't try to assemble or link - just emit the assembly file.
    if (opts_.emit_asm && !opts_.output_asm_path.empty()) {
        result.exit_code = 0;
        return finish();
    }

    // Check if -o path looks like an executable (ends with .exe or has no extension)
    // vs an object file (ends with .o or .obj)
    auto looksLikeObjectFile = [](const std::string &path) -> bool {
        const std::size_t dotPos = path.rfind('.');
        if (dotPos == std::string::npos)
            return false; // No extension - treat as executable
        const std::string ext = path.substr(dotPos);
        return ext == ".o" || ext == ".obj";
    };

    const bool wantsObjectOnly = !opts_.output_obj_path.empty() && !opts_.run_native &&
                                 looksLikeObjectFile(opts_.output_obj_path);
    if (wantsObjectOnly) {
        const std::filesystem::path objPath(opts_.output_obj_path);
        const int assembleExit = invokeAssembler(asmPath, objPath, targetPlatform, out, err);
        if (assembleExit != 0) {
            result.exit_code = assembleExit == -1 ? 1 : assembleExit;
        } else {
            result.exit_code = 0;
            // Clean up intermediate assembly after successful object file creation.
            if (!opts_.emit_asm) {
                std::error_code ec;
                std::filesystem::remove(asmPath, ec);
            }
        }
        return finish();
    }

    // Link to executable if: running native, no output path specified, or output looks like .exe
    const bool needsExecutable = opts_.run_native || opts_.output_obj_path.empty() ||
                                 !looksLikeObjectFile(opts_.output_obj_path);
    if (!needsExecutable) {
        result.exit_code = 0;
        return finish();
    }

    const std::filesystem::path exePath = opts_.output_obj_path.empty()
                                              ? deriveExecutablePath(opts_)
                                              : std::filesystem::path(opts_.output_obj_path);
    std::filesystem::path objPath = std::filesystem::path(opts_.input_il_path);
    objPath.replace_extension(".o");

    const int assembleExit = invokeAssembler(asmPath, objPath, targetPlatform, out, err);
    if (assembleExit != 0) {
        result.exit_code = assembleExit == -1 ? 1 : assembleExit;
        return finish();
    }

    common::LinkContext ctx;
    if (const int rc = common::prepareLinkContext(asmPath.string(), ctx, out, err); rc != 0) {
        result.exit_code = 1;
        return finish();
    }

    if (opts_.link_mode == LinkMode::System)
        err << "warning: --system-link is deprecated; using the native linker\n";

    const int linkExit = linkObjectWithNativeLinker(objPath,
                                                    exePath,
                                                    ctx,
                                                    targetPlatform,
                                                    opts_.extra_objects,
                                                    opts_.stack_size,
                                                    opts_.fast_link,
                                                    opts_.emit_debug_lines,
                                                    opts_.windows_debug_runtime,
                                                    out,
                                                    err);

    {
        std::error_code ec;
        std::filesystem::remove(objPath, ec);
    }
    if (linkExit != 0) {
        result.exit_code = linkExit == -1 ? 1 : linkExit;
        return finish();
    }

    // Clean up the intermediate assembly file after successful linking,
    // unless the user explicitly requested assembly output via -S.
    if (!opts_.emit_asm) {
        std::error_code ec;
        std::filesystem::remove(asmPath, ec);
    }

    if (!opts_.run_native) {
        result.exit_code = 0;
        return finish();
    }

    const int runExit = runExecutable(exePath, out, err);
    if (runExit == -1) {
        result.exit_code = 1;
    } else {
        result.exit_code = runExit;
    }
    return finish();
}

} // namespace viper::codegen::x64
