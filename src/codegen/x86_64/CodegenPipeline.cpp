//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/CodegenPipeline.cpp
// Purpose: Implement the reusable IL-to-x86-64 compilation pipeline used by CLI front ends.
// Key invariants: Passes execute sequentially with early exits on failure, ensuring diagnostics
//                 are recorded deterministically and no partial artefacts leak on error.
// Ownership/Lifetime: The pipeline borrows IL modules and writes assembly/binaries to caller-
//                     specified locations without assuming ownership of external resources.
// Links: docs/codemap.md, src/tools/common/module_loader.hpp
// Cross-platform touchpoints: host ABI selection, native-link fallback rules,
//                             system tool invocation, and runtime archive
//                             composition.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implementation of the high-level IL-to-native compilation pipeline.
/// @details Coordinates module loading, verification, backend pass execution,
///          and optional linking/execution so command-line tools can rely on a
///          single entry point for x86-64 code generation.

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
#include "codegen/x86_64/passes/RegAllocPass.hpp"
#include "il/transform/PassManager.hpp"
#include "tools/common/module_loader.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace viper::codegen::x64 {
namespace {

/// @brief Platform-specific C compiler command.
/// @details On Windows, `cc` isn't available, so we use `clang` instead.
///          On Unix-like systems, `cc` is typically a symlink to the default compiler.
#if defined(_WIN32)
constexpr const char *kCcCommand = "clang";
#else
constexpr const char *kCcCommand = "cc";
#endif

constexpr std::size_t kLargeModuleIlOptThreshold = 100000;

std::filesystem::path pickFirstExisting(std::initializer_list<std::filesystem::path> candidates) {
    for (const auto &candidate : candidates) {
        if (common::fileExists(candidate))
            return candidate;
    }
    return std::filesystem::path{};
}

std::size_t totalInstructionCount(const il::core::Module &module) {
    std::size_t totalInstrs = 0;
    for (const auto &fn : module.functions)
        for (const auto &bb : fn.blocks)
            totalInstrs += bb.instructions.size();
    return totalInstrs;
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
    std::filesystem::path exe = std::filesystem::path(opts.input_il_path);
    if (exe.empty()) {
#if defined(_WIN32)
        return std::filesystem::path("a.exe");
#else
        return std::filesystem::path("a.out");
#endif
    }
    exe.replace_extension("");
    if (exe.filename().empty() || exe.filename() == ".") {
#if defined(_WIN32)
        return exe.parent_path() / "a.exe";
#else
        return exe.parent_path() / "a.out";
#endif
    }
#if defined(_WIN32)
    exe.replace_extension(".exe");
#endif
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
                    std::ostream &out,
                    std::ostream &err) {
    std::vector<std::string> ccArgs = {kCcCommand};
#if defined(__APPLE__)
    ccArgs.push_back("-arch");
    ccArgs.push_back("x86_64");
#endif
    const int exitCode =
        common::invokeAssembler(ccArgs, toNativePath(asmPath), toNativePath(objPath), out, err);
    if (exitCode != 0) {
        err << "error: " << kCcCommand << " (assemble) exited with status " << exitCode << "\n";
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
        appendComponent(RtComponent::Collections);
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

}

int linkObjectWithNativeLinker(const std::filesystem::path &objPath,
                               const std::filesystem::path &exePath,
                               const common::LinkContext &ctx,
                               std::size_t stackSize,
                               std::ostream &out,
                               std::ostream &err) {
    linker::NativeLinkerOptions linkOpts;
    linkOpts.objPath = objPath.string();
    linkOpts.exePath = exePath.string();
    linkOpts.entrySymbol = "main";
    linkOpts.platform = linker::detectLinkPlatform();
    linkOpts.arch = linker::LinkArch::X86_64;
    linkOpts.stackSize = stackSize;
    collectNativeLinkArchives(ctx, linkOpts.archivePaths);
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

    il::core::Module module;
    const auto loadResult = il::tools::common::loadModuleFromFile(opts_.input_il_path, module, err);
    if (!loadResult.succeeded()) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }
    if (!il::tools::common::verifyModule(module, err)) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    viper::codegen::common::lowerNativeEh(module);

    // Run IL optimizations before lowering to MIR.
    // Keep these pipelines aligned with the currently safe canonical O1/O2
    // policies: mem2reg, LICM, and IL peephole remain disabled here too until
    // their rehab pipelines are proven sound.
    if (opts_.optimize >= 1) {
        il::transform::PassManager ilpm;
        const std::size_t totalInstrs = totalInstructionCount(module);
        if (totalInstrs <= kLargeModuleIlOptThreshold) {
            bool ok = true;
            if (opts_.optimize >= 2) {
                ilpm.registerPipeline(
                    "codegen-O2",
                    {"loop-simplify", "loop-rotate",  "indvars",           "loop-unroll",
                     "simplify-cfg",  "sccp",         "check-opt",         "eh-opt",
                     "dce",           "simplify-cfg", "sibling-recursion", "inline",
                     "simplify-cfg",  "sccp",         "constfold",         "dce",
                     "simplify-cfg",  "gvn",          "reassociate",       "earlycse",
                     "dse",           "dce",          "late-cleanup"});
                ok = ilpm.runPipeline(module, "codegen-O2");
            } else {
                ilpm.registerPipeline("codegen-O1",
                                      {"simplify-cfg",
                                       "sccp",
                                       "constfold",
                                       "dce",
                                       "simplify-cfg",
                                       "sccp",
                                       "inline",
                                       "dce",
                                       "simplify-cfg"});
                ok = ilpm.runPipeline(module, "codegen-O1");
            }

            if (!ok) {
                err << "error: failed to run x86-64 IL optimization pipeline\n";
                result.exit_code = 1;
                result.stdout_text = out.str();
                result.stderr_text = err.str();
                return result;
            }
        }
        // Re-verify IL after optimization to catch optimizer bugs early.
        if (!il::tools::common::verifyModule(module, err)) {
            err << "error: IL verification failed after optimization\n";
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }
    }

    passes::Module pipelineModule{};
    pipelineModule.il = std::move(module);

    const bool useNativeAsm = (opts_.assembler_mode == AssemblerMode::Native);
    CodegenOptions codegenOpts{};
    codegenOpts.optimizeLevel = opts_.optimize;
    codegenOpts.targetABI = opts_.target_abi;
    codegenOpts.debugSourcePath = opts_.input_il_path;
    pipelineModule.options = codegenOpts;
    pipelineModule.target = &selectTarget(pipelineModule.options.targetABI);

    passes::Diagnostics diagnostics{};
    passes::PassManager manager{};
    manager.addPass(std::make_unique<passes::LoweringPass>());
    manager.addPass(std::make_unique<passes::LegalizePass>());
    manager.addPass(std::make_unique<passes::RegAllocPass>());
    manager.addPass(std::make_unique<passes::PeepholePass>());

    if (useNativeAsm) {
#if defined(__APPLE__)
        constexpr bool isDarwin = true;
#else
        constexpr bool isDarwin = false;
#endif
        manager.addPass(std::make_unique<passes::BinaryEmitPass>(isDarwin, codegenOpts));
    } else {
        manager.addPass(std::make_unique<passes::EmitPass>(codegenOpts));
    }

    if (!manager.run(pipelineModule, diagnostics)) {
        diagnostics.flush(err);
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
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

#if defined(__APPLE__)
                constexpr const char *blobLabel = "_viper_asset_blob";
                constexpr const char *sizeLabel = "_viper_asset_blob_size";
#else
                constexpr const char *blobLabel = "viper_asset_blob";
                constexpr const char *sizeLabel = "viper_asset_blob_size";
#endif
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
        if (!pipelineModule.binaryText) {
            err << "error: binary emit pass did not produce machine code\n";
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
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
        auto writer =
            objfile::createObjectFileWriter(objfile::detectHostFormat(), objfile::ObjArch::X86_64);
        if (!writer) {
            err << "error: no native object file writer for this platform\n";
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        // Pass pre-encoded DWARF .debug_line data to the writer.
        if (!pipelineModule.debugLineData.empty())
            writer->setDebugLineData(std::move(pipelineModule.debugLineData));

        if (!writer->write(objPath.string(),
                           pipelineModule.binaryTextSections,
                           *pipelineModule.binaryRodata,
                           err)) {
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        if (wantsObjectOnly) {
            result.exit_code = 0;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        // Link the emitted object with the native linker, deriving the runtime
        // archive set from the binary symbol table because there is no assembly
        // text on the native-assembler path.
        const std::filesystem::path exePath = opts_.output_obj_path.empty()
                                                  ? deriveExecutablePath(opts_)
                                                  : std::filesystem::path(opts_.output_obj_path);

        std::unordered_set<std::string> extSymbols;
        for (const auto &sym : pipelineModule.binaryText->symbols()) {
            if (sym.binding == objfile::SymbolBinding::External)
                extSymbols.insert(sym.name);
        }

        common::LinkContext ctx;
        if (const int rc = common::prepareLinkContextFromSymbols(extSymbols, ctx, out, err);
            rc != 0) {
            result.exit_code = 1;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        if (opts_.link_mode == LinkMode::System)
            err << "warning: --system-link is deprecated; using the native linker\n";

        const int linkExit =
            linkObjectWithNativeLinker(objPath, exePath, ctx, opts_.stack_size, out, err);
        if (linkExit != 0) {
            result.exit_code = linkExit == -1 ? 1 : linkExit;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        // Clean up intermediate .o file.
        {
            std::error_code ec;
            std::filesystem::remove(objPath, ec);
        }

        if (!opts_.run_native) {
            result.exit_code = 0;
            result.stdout_text = out.str();
            result.stderr_text = err.str();
            return result;
        }

        const int runExit = runExecutable(exePath, out, err);
        result.exit_code = (runExit == -1) ? 1 : runExit;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // --- System assembler path (existing): text assembly → cc -c → .o ---
    if (!pipelineModule.codegenResult) {
        err << "error: emit pass did not produce assembly output\n";
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    const std::string &asmText = pipelineModule.codegenResult->asmText;

    const std::filesystem::path asmPath = opts_.output_asm_path.empty()
                                              ? deriveAssemblyPath(opts_)
                                              : std::filesystem::path(opts_.output_asm_path);
    if (!writeAssemblyFile(asmPath, asmText, err)) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // If user requested assembly output via -S with a specific path, stop here.
    // Don't try to assemble or link - just emit the assembly file.
    if (opts_.emit_asm && !opts_.output_asm_path.empty()) {
        result.exit_code = 0;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
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
        const int assembleExit = invokeAssembler(asmPath, objPath, out, err);
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
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // Link to executable if: running native, no output path specified, or output looks like .exe
    const bool needsExecutable = opts_.run_native || opts_.output_obj_path.empty() ||
                                 !looksLikeObjectFile(opts_.output_obj_path);
    if (!needsExecutable) {
        result.exit_code = 0;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    const std::filesystem::path exePath = opts_.output_obj_path.empty()
                                              ? deriveExecutablePath(opts_)
                                              : std::filesystem::path(opts_.output_obj_path);
    std::filesystem::path objPath = std::filesystem::path(opts_.input_il_path);
    objPath.replace_extension(".o");

    const int assembleExit = invokeAssembler(asmPath, objPath, out, err);
    if (assembleExit != 0) {
        result.exit_code = assembleExit == -1 ? 1 : assembleExit;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    common::LinkContext ctx;
    if (const int rc = common::prepareLinkContext(asmPath.string(), ctx, out, err); rc != 0) {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    if (opts_.link_mode == LinkMode::System)
        err << "warning: --system-link is deprecated; using the native linker\n";

    const int linkExit =
        linkObjectWithNativeLinker(objPath, exePath, ctx, opts_.stack_size, out, err);

    {
        std::error_code ec;
        std::filesystem::remove(objPath, ec);
    }
    if (linkExit != 0) {
        result.exit_code = linkExit == -1 ? 1 : linkExit;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // Clean up the intermediate assembly file after successful linking,
    // unless the user explicitly requested assembly output via -S.
    if (!opts_.emit_asm) {
        std::error_code ec;
        std::filesystem::remove(asmPath, ec);
    }

    if (!opts_.run_native) {
        result.exit_code = 0;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    const int runExit = runExecutable(exePath, out, err);
    if (runExit == -1) {
        result.exit_code = 1;
    } else {
        result.exit_code = runExit;
    }
    result.stdout_text = out.str();
    result.stderr_text = err.str();
    return result;
}

} // namespace viper::codegen::x64
