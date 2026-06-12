//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/CodegenPipeline.cpp
// Purpose: Implements the modular AArch64 code-generation pipeline by wiring
//          all AArch64 passes through the PassManager in the correct order and
//          providing a reusable end-to-end pipeline for the CLI.
// Key invariants:
//   - Pass order: Lowering → PreRegAllocOpt → Legalize → RegAlloc →
//     BlockLayout → Peephole → Scheduler → Peephole → Emit.
//   - Host/native-link availability, runtime archive composition, and system
//     tool invocation are selected at runtime based on TargetPlatform.
// Cross-platform touchpoints:
//   - Native-link archive discovery and platform-specific linker options are
//     routed through codegen/common/LinkerSupport and NativeLinker.
// Ownership/Lifetime:
//   - CodegenPipeline owns the Options struct; all other objects are
//     stack-local or owned by the caller-supplied AArch64Module.
// Links: codegen/aarch64/CodegenPipeline.hpp,
//        codegen/aarch64/passes/PassManager.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/CodegenPipeline.hpp"

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/passes/BinaryEmitPass.hpp"
#include "codegen/aarch64/passes/BlockLayoutPass.hpp"
#include "codegen/aarch64/passes/EmitPass.hpp"
#include "codegen/aarch64/passes/LegalizePass.hpp"
#include "codegen/aarch64/passes/LoweringPass.hpp"
#include "codegen/aarch64/passes/PeepholePass.hpp"
#include "codegen/aarch64/passes/PreRegAllocOptPass.hpp"
#include "codegen/aarch64/passes/RegAllocPass.hpp"
#include "codegen/aarch64/passes/SchedulerPass.hpp"
#include "codegen/common/LinkerSupport.hpp"
#include "codegen/common/NativeEHLowering.hpp"
#include "codegen/common/linker/NativeLinker.hpp"
#include "codegen/common/objfile/ObjectFileWriter.hpp"
#include "common/RunProcess.hpp"
#include "il/transform/PassManager.hpp"
#include "tools/common/module_loader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace viper::codegen::aarch64 {

namespace {

using viper::codegen::common::LinkContext;
using TargetPlatform = CodegenPipeline::TargetPlatform;

/// @brief Dump all MIR functions to the provided stream with a header tag.
static void dumpMir(const passes::AArch64Module &module, const char *tag, std::ostream &os) {
    for (const auto &fn : module.mir) {
        os << "=== MIR " << tag << ": " << fn.name << " ===\n";
        os << toString(fn) << "\n";
    }
}

/// @brief Map a TargetPlatform to the object-file format used by that OS.
static objfile::ObjFormat targetObjectFormat(TargetPlatform platform) {
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

/// @brief Map a TargetPlatform to the linker's LinkPlatform enum.
static linker::LinkPlatform targetLinkPlatform(TargetPlatform platform) {
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

/// @brief Return the system assembler command-line prefix for a given target platform.
/// On Darwin builds a `cc -arch arm64` invocation; on cross-compile hosts uses
/// the clang `--target=` triple matching the AArch64 ABI of the target OS.
static std::vector<std::string> systemAssemblerArgs(TargetPlatform platform) {
    switch (platform) {
        case TargetPlatform::Darwin:
#if defined(__APPLE__)
            return {"cc", "-arch", "arm64"};
#else
            return {"clang", "--target=arm64-apple-macos11"};
#endif
        case TargetPlatform::Linux:
            return {"clang", "--target=aarch64-unknown-linux-gnu"};
        case TargetPlatform::Windows:
            return {"clang", "--target=aarch64-pc-windows-msvc"};
        case TargetPlatform::Host:
            return systemAssemblerArgs(
                targetLinkPlatform(TargetPlatform::Host) == linker::LinkPlatform::macOS
                    ? TargetPlatform::Darwin
                : targetLinkPlatform(TargetPlatform::Host) == linker::LinkPlatform::Windows
                    ? TargetPlatform::Windows
                    : TargetPlatform::Linux);
    }
    return {"clang", "--target=aarch64-unknown-linux-gnu"};
}

/// @brief Return true if @p path has a .o or .obj extension (object file output).
static bool isObjectOutputPath(const std::string &path) {
    const std::string ext = std::filesystem::path(path).extension().string();
    return ext == ".o" || ext == ".obj";
}

/// @brief Forward declaration; defined later after collectNativeLinkArchives.
static int linkObjToExe(const std::string &objPath,
                        const std::string &exePath,
                        const LinkContext &ctx,
                        TargetPlatform targetPlatform,
                        std::size_t stackSize,
                        const std::vector<std::string> &extraObjects,
                        bool fastLink,
                        std::optional<bool> windowsDebugRuntime,
                        bool preserveDebugSections,
                        std::ostream &out,
                        std::ostream &err);

/// @brief Assemble @p asmPath to a temporary .o, then link to @p exePath via
///        the system assembler. Cleans up the temporary object file on return.
static int linkToExe(const std::string &asmPath,
                     const std::string &exePath,
                     TargetPlatform targetPlatform,
                     std::size_t stackSize,
                     const std::vector<std::string> &extraObjects,
                     std::optional<bool> windowsDebugRuntime,
                     bool preserveDebugSections,
                     std::ostream &out,
                     std::ostream &err) {
    using namespace viper::codegen::common;

    LinkContext ctx;
    if (const int rc = prepareLinkContext(asmPath, ctx, out, err); rc != 0)
        return rc;

    std::filesystem::path objPath = std::filesystem::path(asmPath);
    objPath.replace_extension(".o");
    const int arc =
        invokeAssembler(systemAssemblerArgs(targetPlatform), asmPath, objPath.string(), out, err);
    if (arc != 0)
        return arc;

    const int lrc = linkObjToExe(objPath.string(),
                                 exePath,
                                 ctx,
                                 targetPlatform,
                                 stackSize,
                                 extraObjects,
                                 false,
                                 windowsDebugRuntime,
                                 preserveDebugSections,
                                 out,
                                 err);
    std::error_code ec;
    std::filesystem::remove(objPath, ec);
    return lrc;
}

/// @brief Populate @p archives with all runtime archive paths required for linking.
/// @details Deduplicates by absolute path. When the requested target platform is
///          Windows, always pulls in Oop/Arrays/Collections/Threads/Text/IoFs
///          alongside the Base archive because the Windows CRT startup expects
///          them to be present. Graphics and Audio support libraries are
///          appended when the link context declares those components.
/// @param ctx Link context produced by prepareLinkContext/prepareLinkContextFromSymbols.
/// @param targetPlatform Requested OS platform; Host is resolved through targetLinkPlatform().
/// @param archives Output list; entries are absolute paths appended in dependency order.
static void collectNativeLinkArchives(const common::LinkContext &ctx,
                                      TargetPlatform targetPlatform,
                                      std::vector<std::string> &archives) {
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

    if (targetLinkPlatform(targetPlatform) == linker::LinkPlatform::Windows &&
        common::hasComponent(ctx, RtComponent::Base)) {
        appendComponent(RtComponent::Oop);
        appendComponent(RtComponent::Arrays);
        appendComponent(RtComponent::Collections);
        appendComponent(RtComponent::Threads);
        appendComponent(RtComponent::Text);
        appendComponent(RtComponent::IoFs);
    }

    if (common::hasComponent(ctx, RtComponent::Graphics)) {
        appendIfExists(common::supportLibraryPath(ctx.buildDir, "vipergui"));
        appendIfExists(common::supportLibraryPath(ctx.buildDir, "vipergfx"));
    }

    if (common::hasComponent(ctx, RtComponent::Audio))
        appendIfExists(common::supportLibraryPath(ctx.buildDir, "viperaud"));

    // Embedding the Zia frontend pulls in il_runtime's RuntimeRegistry, which
    // references the entire rt_* catalog regardless of what the program itself
    // uses. Link every runtime component so those references resolve.
    if (ctx.needsZiaFrontend) {
        for (int i = 0; i < static_cast<int>(RtComponent::Count); ++i)
            appendComponent(static_cast<RtComponent>(i));
    }
}

/// @brief Link a single .o file into a native executable using the Viper native linker.
/// @details Fills a NativeLinkerOptions struct from the link context (runtime archives,
///          extra objects, stack size) and dispatches to nativeLink(). AArch64 arch is always
///          used; platform is mapped from the TargetPlatform enum.
/// @param objPath       Path to the compiled object file.
/// @param exePath       Destination path for the output executable.
/// @param ctx           Link context (runtime archive set, build dir, etc.).
/// @param targetPlatform Target OS; determines symbol mangling and ABI format.
/// @param stackSize     Requested thread stack size in bytes; 0 means platform default.
/// @param extraObjects  Additional .o files to include in the link (e.g. runtime stubs).
/// @param fastLink      Skip non-essential size-reduction passes in the linker.
/// @param preserveDebugSections Keep non-alloc DWARF/debug sections in linked output.
/// @param out           Stream for linker stdout diagnostics.
/// @param err           Stream for linker stderr diagnostics.
/// @return 0 on success, non-zero on link failure.
static int linkObjToExe(const std::string &objPath,
                        const std::string &exePath,
                        const LinkContext &ctx,
                        TargetPlatform targetPlatform,
                        std::size_t stackSize,
                        const std::vector<std::string> &extraObjects,
                        bool fastLink,
                        std::optional<bool> windowsDebugRuntime,
                        bool preserveDebugSections,
                        std::ostream &out,
                        std::ostream &err) {
    using namespace viper::codegen::common;
    using viper::codegen::archiveNameForComponent;
    using viper::codegen::RtComponent;

    linker::NativeLinkerOptions linkOpts;
    linkOpts.objPath = objPath;
    linkOpts.exePath = exePath;
    linkOpts.platform = targetLinkPlatform(targetPlatform);
    linkOpts.arch = linker::LinkArch::AArch64;
    linkOpts.entrySymbol = "main";
    linkOpts.stackSize = stackSize;
    linkOpts.fastLink = fastLink;
    linkOpts.preserveDebugSections = preserveDebugSections;
    linkOpts.windowsDebugRuntime = windowsDebugRuntime;
    linkOpts.extraObjPaths = extraObjects;
    collectNativeLinkArchives(ctx, targetPlatform, linkOpts.archivePaths);
    if (ctx.needsZiaFrontend) {
        const auto ziaEditorLib = common::supportLibraryPath(ctx.buildDir, "zia_editor_services");
        if (common::fileExists(ziaEditorLib))
            linkOpts.forceLoadArchivePaths.push_back(ziaEditorLib.lexically_normal().string());
        // zia_editor_services' static-link closure (fe_zia plus IL
        // build/verify/transform/runtime/core/support). Demand-driven: only
        // referenced members are extracted.
        for (const auto &lib : common::ziaFrontendClosureLibs()) {
            const auto p = common::supportLibraryPath(ctx.buildDir, lib);
            if (common::fileExists(p))
                linkOpts.archivePaths.push_back(p.lexically_normal().string());
        }
    }

    return viper::codegen::linker::nativeLink(linkOpts, out, err);
}

/// @brief Run IL-level optimization passes on @p mod before machine-code lowering.
/// @details Skips all work when optimizeLevel < 1. At O1 the "O1" preset is used;
///          at O2+ the "O2" preset is used. The IL PassManager applies DCE, inlining,
///          constant folding, and SimplifyCFG in the selected order.
/// @param mod           Module to optimize in place.
/// @param optimizeLevel 0 = no optimization; 1 = O1; 2+ = O2.
/// @return true on success, false if any IL optimizer pass returns an error.
static bool runIlOptimizations(il::core::Module &mod, int optimizeLevel) {
    if (optimizeLevel < 1)
        return true;

    il::transform::PassManager ilpm;
    return ilpm.runPipeline(mod, optimizeLevel >= 2 ? "O2" : "O1");
}

/// @brief Return the AArch64 TargetInfo for the host OS detected at compile time.
/// @details Compile-time dispatch: _WIN32 → Windows AAPCS64/COFF, __APPLE__ → Darwin
///          AAPCS64/Mach-O with BTI+PAC, otherwise Linux AAPCS64/ELF.
/// @return Reference to the appropriate singleton target; lifetime is static.
static const TargetInfo &hostAArch64Target() {
#if defined(_WIN32)
    return windowsTarget();
#elif defined(__APPLE__)
    return darwinTarget();
#else
    return linuxTarget();
#endif
}

/// @brief Map a TargetPlatform enum value to the corresponding AArch64 TargetInfo singleton.
/// @param platform Darwin, Linux, Windows, or Host (auto-detected via hostAArch64Target()).
/// @return Const reference to the selected singleton; lifetime is static.
static const TargetInfo &selectAArch64Target(CodegenPipeline::TargetPlatform platform) {
    switch (platform) {
        case CodegenPipeline::TargetPlatform::Darwin:
            return darwinTarget();
        case CodegenPipeline::TargetPlatform::Linux:
            return linuxTarget();
        case CodegenPipeline::TargetPlatform::Windows:
            return windowsTarget();
        case CodegenPipeline::TargetPlatform::Host:
            return hostAArch64Target();
    }
    return hostAArch64Target();
}

} // namespace

/// @brief Run the full AArch64 codegen pipeline: lower → legalize → regalloc → optimize → emit.
/// @details Orchestrates all passes via PassManager in order: IL-to-MIR lowering,
///          pre-RA MIR legalization, register allocation (with optional coalescing),
///          post-RA layout/peephole/scheduling (O1+), and assembly/binary emission.
bool runCodegenPipeline(passes::AArch64Module &module,
                        const PipelineOptions &opts,
                        std::ostream &diagOut) {
    passes::Diagnostics diags;
    auto flushOnFailure = [&]() {
        diags.flush(diagOut);
        return false;
    };

    {
        passes::PassManager manager;
        if (opts.timePasses)
            manager.setTimingStream(&diagOut, "aarch64");
        manager.addPass(std::make_unique<passes::LoweringPass>());
        manager.addPass(std::make_unique<passes::LegalizePass>());
        if (opts.optimizeLevel >= 1)
            manager.addPass(std::make_unique<passes::PreRegAllocOptPass>());
        if (!manager.run(module, diags))
            return flushOnFailure();
    }

    if (opts.dumpMirBeforeRA)
        dumpMir(module, "before RA", diagOut);

    {
        passes::PassManager manager;
        if (opts.timePasses)
            manager.setTimingStream(&diagOut, "aarch64");
        manager.addPass(std::make_unique<passes::RegAllocPass>());
        if (!manager.run(module, diags))
            return flushOnFailure();
    }

    if (opts.dumpMirAfterRA)
        dumpMir(module, "after RA", diagOut);

    if (opts.optimizeLevel >= 1) {
        passes::PassManager manager;
        if (opts.timePasses)
            manager.setTimingStream(&diagOut, "aarch64");
        manager.addPass(std::make_unique<passes::BlockLayoutPass>());
        manager.addPass(std::make_unique<passes::PeepholePass>());
        manager.addPass(std::make_unique<passes::SchedulerPass>());
        manager.addPass(std::make_unique<passes::PeepholePass>(
            passes::PeepholePass::Mode::PostScheduleCleanup));
        if (!manager.run(module, diags))
            return flushOnFailure();
    }

    if (opts.dumpMirAfterRA && opts.optimizeLevel >= 1)
        dumpMir(module, "after peephole", diagOut);

    {
        passes::PassManager manager;
        if (opts.timePasses)
            manager.setTimingStream(&diagOut, "aarch64");
        if (opts.emitAssemblyText)
            manager.addPass(std::make_unique<passes::EmitPass>());
        if (opts.useBinaryEmit)
            manager.addPass(std::make_unique<passes::BinaryEmitPass>());
        if (!manager.run(module, diags))
            return flushOnFailure();
    }

    diags.flush(diagOut);
    return true;
}

/// @brief Construct a CodegenPipeline, capturing all options by value.
/// @param opts Full pipeline configuration; moved into opts_ to avoid a copy.
CodegenPipeline::CodegenPipeline(Options opts) : opts_(std::move(opts)) {}

/// @brief Run the pipeline reading the IL module from Options::input_il_path.
/// @details Loads and verifies the module from disk, then delegates to runWithModule()
///          with moduleAlreadyVerified = true to avoid a redundant verification pass.
/// @return PipelineResult with exit_code 0 on success; stderr_text holds any diagnostics.
PipelineResult CodegenPipeline::run() {
    PipelineResult result{};
    std::ostringstream out;
    std::ostringstream err;
    auto finish = [&]() -> PipelineResult {
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    };

    il::core::Module mod;
    const auto load = il::tools::common::loadModuleFromFile(opts_.input_il_path, mod, err);
    if (!load.succeeded()) {
        result.exit_code = 1;
        return finish();
    }
    if (!il::tools::common::verifyModule(mod, err)) {
        result.exit_code = 1;
        return finish();
    }

    return runWithModule(std::move(mod), opts_.input_il_path, true);
}

/// @brief Run the pipeline using an already-loaded IL module.
/// @details Entry point used by both run() and by callers that own the module (e.g. the
///          REPL or test harness). Steps: optional re-verification → EH lowering →
///          IL optimization → target selection → MIR codegen → assembly/object emission →
///          optional native linking → optional execution.
/// @param mod                    IL module to compile; consumed/moved into the pipeline.
/// @param debugSourcePath        Source path embedded in debug line-number directives.
///                               Falls back to opts_.input_il_path when empty.
/// @param moduleAlreadyVerified  When true, skips the IL verifier pass at entry
///                               (saves a redundant O(n) traversal when run() already verified).
/// @return PipelineResult with exit_code 0 on success; stderr_text holds any diagnostics.
PipelineResult CodegenPipeline::runWithModule(il::core::Module mod,
                                              std::string debugSourcePath,
                                              bool moduleAlreadyVerified) {
    PipelineResult result{};
    std::ostringstream out;
    std::ostringstream err;
    auto finish = [&]() -> PipelineResult {
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    };

    if (debugSourcePath.empty())
        debugSourcePath = opts_.input_il_path;

    if (!moduleAlreadyVerified && !il::tools::common::verifyModule(mod, err)) {
        result.exit_code = 1;
        return finish();
    }

    viper::codegen::common::lowerNativeEh(mod);
    if (const auto residualEh = viper::codegen::common::findResidualStructuredEh(mod)) {
        err << "error: " << *residualEh << "\n";
        result.exit_code = 1;
        return finish();
    }

    if (!opts_.skip_il_optimization && !runIlOptimizations(mod, opts_.optimize)) {
        err << "error: failed to run AArch64 IL optimization pipeline\n";
        result.exit_code = 1;
        return finish();
    }
    if (!opts_.skip_il_optimization && opts_.optimize >= 1 &&
        !il::tools::common::verifyModule(mod, err)) {
        err << "error: IL verification failed after optimization\n";
        result.exit_code = 1;
        return finish();
    }

    if (opts_.run_native) {
        if (opts_.target_platform != TargetPlatform::Host) {
            err << "error: --run-native requires --target-host on the AArch64 backend\n";
            result.exit_code = 1;
            return finish();
        }
#if !(defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__)))
        err << "error: --run-native is only supported on macOS arm64 hosts\n";
        result.exit_code = 1;
        return finish();
#endif
    }

    const TargetInfo &ti = selectAArch64Target(opts_.target_platform);

    passes::AArch64Module pipelineModule;
    pipelineModule.ilMod = &mod;
    pipelineModule.ti = &ti;
    pipelineModule.debugSourcePath = debugSourcePath;
    pipelineModule.emitDebugLines = opts_.emit_debug_lines;
    pipelineModule.coalesceTextSections = opts_.fast_link || opts_.optimize == 0;

    PipelineOptions pipeOpts;
    pipeOpts.dumpMirBeforeRA = opts_.dump_mir_before_ra;
    pipeOpts.dumpMirAfterRA = opts_.dump_mir_after_ra;
    pipeOpts.emitAssemblyText = opts_.assembler_mode == AssemblerMode::System || opts_.emit_asm ||
                                (opts_.output_obj_path.empty() && !opts_.run_native);
    pipeOpts.useBinaryEmit = opts_.assembler_mode == AssemblerMode::Native;
    pipeOpts.optimizeLevel = opts_.optimize;
    pipeOpts.timePasses = opts_.time_passes;

    if (!runCodegenPipeline(pipelineModule, pipeOpts, err)) {
        result.exit_code = 1;
        return finish();
    }

    std::string asmText = pipelineModule.assembly;
    std::string asmPath = opts_.output_asm_path;
    if (asmPath.empty()) {
        std::filesystem::path p(opts_.input_il_path);
        p.replace_extension(".s");
        asmPath = p.string();
    }

    if (opts_.emit_asm) {
        if (!common::writeTextFile(asmPath, asmText, err)) {
            result.exit_code = 1;
            return finish();
        }
    }

    if (opts_.output_obj_path.empty() && !opts_.run_native) {
        if (!opts_.emit_asm && !common::writeTextFile(asmPath, asmText, err)) {
            result.exit_code = 1;
            return finish();
        }
        return finish();
    }

    // --- Inject asset blob into .rodata (if present) ---
    if (pipelineModule.binaryRodata && !opts_.asset_blob_path.empty()) {
        std::ifstream af(opts_.asset_blob_path, std::ios::binary | std::ios::ate);
        if (!af.is_open()) {
            err << "error: failed to open asset blob '" << opts_.asset_blob_path << "'\n";
            result.exit_code = 1;
            return finish();
        }
        const std::streampos blobSizePos = af.tellg();
        if (blobSizePos < 0) {
            err << "error: failed to determine asset blob size '" << opts_.asset_blob_path << "'\n";
            result.exit_code = 1;
            return finish();
        }
        const auto blobSizeU64 = static_cast<uint64_t>(blobSizePos);
        if (blobSizeU64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            err << "error: asset blob '" << opts_.asset_blob_path
                << "' is too large for this host\n";
            result.exit_code = 1;
            return finish();
        }
        const auto blobSize = static_cast<size_t>(blobSizeU64);
        if (blobSize > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
            err << "error: asset blob '" << opts_.asset_blob_path
                << "' is too large for a single stream read\n";
            result.exit_code = 1;
            return finish();
        }
        std::vector<uint8_t> assetBlob(blobSize);
        af.seekg(0);
        if (!af.good()) {
            err << "error: failed to seek asset blob '" << opts_.asset_blob_path << "'\n";
            result.exit_code = 1;
            return finish();
        }
        if (blobSize != 0) {
            af.read(reinterpret_cast<char *>(assetBlob.data()),
                    static_cast<std::streamsize>(assetBlob.size()));
            if (af.gcount() != static_cast<std::streamsize>(assetBlob.size())) {
                err << "error: failed to read complete asset blob '" << opts_.asset_blob_path
                    << "'\n";
                result.exit_code = 1;
                return finish();
            }
        }

        // AArch64 MachO writer adds _ prefix to global symbols automatically.
        auto &rodata = *pipelineModule.binaryRodata;
        rodata.alignTo(16);
        rodata.defineSymbol(
            "viper_asset_blob", objfile::SymbolBinding::Global, objfile::SymbolSection::Rodata);
        rodata.emitBytes(assetBlob.data(), assetBlob.size());
        rodata.alignTo(8);
        rodata.defineSymbol("viper_asset_blob_size",
                            objfile::SymbolBinding::Global,
                            objfile::SymbolSection::Rodata);
        rodata.emit64LE(static_cast<uint64_t>(assetBlob.size()));
    }

    if (opts_.assembler_mode == AssemblerMode::Native && pipelineModule.binaryText) {
        std::filesystem::path objPath;
        bool outputIsObj = false;
        if (!opts_.output_obj_path.empty() && isObjectOutputPath(opts_.output_obj_path)) {
            objPath = opts_.output_obj_path;
            outputIsObj = true;
        } else {
            objPath = std::filesystem::path(opts_.input_il_path).replace_extension(".o");
        }

        using namespace viper::codegen::objfile;
        auto writer =
            createObjectFileWriter(targetObjectFormat(opts_.target_platform), ObjArch::AArch64);
        if (!writer) {
            err << "error: no native object file writer for this platform\n";
            result.exit_code = 1;
            return finish();
        }
        const bool hasDebugLine = !pipelineModule.debugLineData.empty();
        if (hasDebugLine)
            writer->setDebugLineData(std::move(pipelineModule.debugLineData));
        const bool wroteObject = !pipelineModule.binaryTextSections.empty()
                                     ? writer->write(objPath.string(),
                                                     pipelineModule.binaryTextSections,
                                                     *pipelineModule.binaryRodata,
                                                     err)
                                     : writer->write(objPath.string(),
                                                     *pipelineModule.binaryText,
                                                     *pipelineModule.binaryRodata,
                                                     err);
        if (!wroteObject) {
            err << "error: failed to write object file '" << objPath.string() << "'\n";
            result.exit_code = 1;
            return finish();
        }

        if (outputIsObj) {
            return finish();
        }

        std::unordered_set<std::string> extSymbols;
        for (const auto &section : pipelineModule.binaryTextSections)
            for (const auto &sym : section.symbols()) {
                if (sym.binding == viper::codegen::objfile::SymbolBinding::External)
                    extSymbols.insert(sym.name);
            }
        if (pipelineModule.binaryRodata) {
            for (const auto &sym : pipelineModule.binaryRodata->symbols()) {
                if (sym.binding == viper::codegen::objfile::SymbolBinding::External)
                    extSymbols.insert(sym.name);
            }
        }

        LinkContext ctx;
        if (const int rc =
                viper::codegen::common::prepareLinkContextFromSymbols(extSymbols, ctx, out, err);
            rc != 0) {
            result.exit_code = 1;
            return finish();
        }

        std::filesystem::path exe =
            opts_.output_obj_path.empty()
                ? std::filesystem::path(opts_.input_il_path).replace_extension("")
                : std::filesystem::path(opts_.output_obj_path);

        if (opts_.link_mode == LinkMode::System)
            err << "warning: --system-link is deprecated; using the native linker\n";

        const int lrc = linkObjToExe(objPath.string(),
                                     exe.string(),
                                     ctx,
                                     opts_.target_platform,
                                     opts_.stack_size,
                                     opts_.extra_objects,
                                     opts_.fast_link,
                                     opts_.windows_debug_runtime,
                                     opts_.emit_debug_lines,
                                     out,
                                     err);

        if (!outputIsObj) {
            std::error_code ec;
            std::filesystem::remove(objPath, ec);
        }

        if (lrc != 0) {
            result.exit_code = 1;
            return finish();
        }

        if (opts_.run_native) {
            const int rc = viper::codegen::common::runExecutable(exe.string(), out, err);
            result.exit_code = rc == -1 ? 1 : rc;
        }

        return finish();
    }

    if (!opts_.emit_asm && !common::writeTextFile(asmPath, asmText, err)) {
        result.exit_code = 1;
        return finish();
    }

    if (!opts_.output_obj_path.empty() && !opts_.run_native) {
        const std::string &outPath = opts_.output_obj_path;
        if (isObjectOutputPath(outPath)) {
            const int arc = viper::codegen::common::invokeAssembler(
                systemAssemblerArgs(opts_.target_platform), asmPath, outPath, out, err);
            result.exit_code = arc == 0 ? 0 : 1;
            return finish();
        }

        if (opts_.link_mode == LinkMode::System)
            err << "warning: --system-link is deprecated; using the native linker\n";
        const int lrc = linkToExe(asmPath,
                                  outPath,
                                  opts_.target_platform,
                                  opts_.stack_size,
                                  opts_.extra_objects,
                                  opts_.windows_debug_runtime,
                                  opts_.emit_debug_lines,
                                  out,
                                  err);
        if (lrc == 0 && !opts_.emit_asm) {
            std::error_code ec;
            std::filesystem::remove(asmPath, ec);
        }
        result.exit_code = lrc == 0 ? 0 : 1;
        return finish();
    }

    std::filesystem::path exe =
        opts_.output_obj_path.empty()
            ? std::filesystem::path(opts_.input_il_path).replace_extension("")
            : std::filesystem::path(opts_.output_obj_path);

    if (opts_.link_mode == LinkMode::System)
        err << "warning: --system-link is deprecated; using the native linker\n";
    if (linkToExe(asmPath,
                  exe.string(),
                  opts_.target_platform,
                  opts_.stack_size,
                  opts_.extra_objects,
                  opts_.windows_debug_runtime,
                  opts_.emit_debug_lines,
                  out,
                  err) != 0) {
        result.exit_code = 1;
        return finish();
    }

    if (!opts_.emit_asm) {
        std::error_code ec;
        std::filesystem::remove(asmPath, ec);
    }

    if (opts_.run_native) {
        const int rc = viper::codegen::common::runExecutable(exe.string(), out, err);
        result.exit_code = rc == -1 ? 1 : rc;
    }

    return finish();
}

} // namespace viper::codegen::aarch64
